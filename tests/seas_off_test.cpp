// The world with no experimental flag (experimental_flags.hpp) is the world of main, bit for bit
// (agent 51, F1: the relief of the wave merged switched off): sums of the small world with its
// planet level and its water, the samples of a lattice over the whole world and of windows at sea
// and on the coasts, the tiles of LOD 0 to 3 and the cells of the horizon there, pinned with the
// values this same program gave on main (ab574e3, where noFlags() returns the default parameters).

#include "dunes_support.hpp"
#include "test_assert.hpp"

#include "terrain/generation/experimental_flags.hpp"
#include "terrain/streaming/horizon_sampler.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace {

using Place = std::pair<double, double>;

constexpr double kContinentalScale = 1'500'000.0; // continentalness to the coast signal

// The parameters of the world without any flag (main: HeightfieldParams{}, HydrologyParams{}).
std::pair<terrain::HeightfieldParams, terrain::HydrologyParams> noFlags() {
    terrain::WorldParams p;
    const terrain::AppliedFlags applied = terrain::applyExperimentalFlags("", p);
    TEST_CHECK(applied.text.empty() && applied.unknown.empty());
    return {p.relief, p.water};
}

class Sum {
public:
    void add(std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            hash_ = (hash_ ^ ((v >> (8 * i)) & 0xFFU)) * 0x100000001B3ULL;
        }
    }
    void add(float v) { add(static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(v))); }
    void add(double v) { add(std::bit_cast<std::uint64_t>(v)); }
    [[nodiscard]] std::uint64_t value() const { return hash_; }

private:
    std::uint64_t hash_ = 0xCBF29CE484222325ULL;
};

struct World {
    std::shared_ptr<const terrain::Hydrology> water;
    std::unique_ptr<terrain::TerrainTiles> tiles;
};

const World& world() {
    static const World w = [] {
        auto [relief, params] = noFlags();
        params.ergs = dunes_test::smallErgParams();
        World out;
        out.water = std::make_shared<const terrain::Hydrology>(
            dunes_test::kSeed, dunes_test::small().relief, dunes_test::small().planet, params);
        out.tiles = std::make_unique<terrain::TerrainTiles>(
            dunes_test::kSeed, relief, terrain::EcologyParams{}, std::nullopt,
            golden::kSmallPlanet, out.water);
        return out;
    }();
    return w;
}

double coastAt(double x, double z) {
    return static_cast<double>(dunes_test::small().relief.sampleAt(x, z).continentalness) *
           kContinentalScale;
}

// Places of a lattice of 64 km whose coast signal lies in [low, high): count of them, spread.
std::vector<Place> placesWhere(double low, double high, std::size_t count) {
    std::vector<Place> all;
    for (double z = -0.5 * golden::kSmallPlanet.z; z < 0.5 * golden::kSmallPlanet.z;
         z += 65'536.0) {
        for (double x = -0.5 * golden::kSmallPlanet.x; x < 0.5 * golden::kSmallPlanet.x;
             x += 65'536.0) {
            const double coast = coastAt(x, z);
            if (coast >= low && coast < high) {
                all.emplace_back(x + 1'234.5, z + 2'345.5);
            }
        }
    }
    std::vector<Place> out;
    const std::size_t stride = std::max<std::size_t>(1, all.size() / count);
    for (std::size_t k = stride / 2; k < all.size() && out.size() < count; k += stride) {
        out.push_back(all[k]);
    }
    return out;
}

// The coasts (the band of agent 33 and the shore), the open sea, the land.
std::vector<Place> places() {
    std::vector<Place> out = placesWhere(-25'000.0, 5'000.0, 8);
    for (const Place& p : placesWhere(-1.0e12, -60'000.0, 6)) {
        out.push_back(p);
    }
    for (const Place& p : placesWhere(60'000.0, 1.0e12, 2)) {
        out.push_back(p);
    }
    return out;
}

void addSample(Sum& sum, const terrain::HeightSample& s) {
    for (const float v : {s.continentalness, s.macroContinentalness, s.plateContinentalness,
                          s.plateCollision, s.mountainMask, s.relief, s.rift, s.base, s.regional,
                          s.mountain, s.detail, s.height, s.waterLevel, s.dunes, s.erg,
                          s.ergFloor, s.rock}) {
        sum.add(v);
    }
    sum.add(static_cast<std::uint64_t>(s.water));
}

// A batch of samples along a row: count those at sea, sum every field.
void sampleRows(const std::vector<double>& xs, double z, Sum& sum, std::size_t& ocean) {
    std::vector<double> zs(xs.size(), z);
    std::vector<terrain::HeightSample> out(xs.size());
    world().tiles->heightfield().samplesAt(xs, zs, out);
    for (const terrain::HeightSample& s : out) {
        addSample(sum, s);
        ocean += s.water == terrain::WaterKind::Ocean ? 1 : 0;
    }
}

std::uint64_t samplesSum(std::size_t& count, std::size_t& ocean) {
    Sum sum;
    const terrain::WorldSize& w = golden::kSmallPlanet;
    // One by one (a batch spread over the world would gather the water of all of it): every
    // 16 km at sea, every 128 km on land (the courses of all the rivers of the world otherwise).
    for (double z = -0.5 * w.z + 8'192.0; z < 0.5 * w.z; z += 16'384.0) {
        for (double x = -0.5 * w.x + 4'096.0; x < 0.5 * w.x; x += 16'384.0) {
            const bool node = std::fmod(x + 0.5 * w.x - 4'096.0, 131'072.0) == 0.0 &&
                              std::fmod(z + 0.5 * w.z - 8'192.0, 131'072.0) == 0.0;
            if (!node && coastAt(x, z) >= 0.0) {
                continue;
            }
            const terrain::HeightSample s = world().tiles->heightfield().sampleAt(x, z);
            addSample(sum, s);
            ocean += s.water == terrain::WaterKind::Ocean ? 1 : 0;
            ++count;
        }
    }
    for (const auto& [x, z] : places()) {
        for (int j = 0; j < 64; ++j) {
            std::vector<double> xs;
            for (int i = 0; i < 64; ++i) {
                xs.push_back(terrain::wrapCoordinate(x + 150.0 * (i - 32), w.x));
            }
            count += xs.size();
            sampleRows(xs, terrain::wrapCoordinate(z + 150.0 * (j - 32), w.z), sum, ocean);
        }
    }
    return sum.value();
}

void addTile(Sum& sum, const terrain::TileData& t) {
    for (const double v : {t.originX, t.originY, t.originZ}) {
        sum.add(v);
    }
    for (std::size_t n = 0; n < t.mesh.vertices.size(); ++n) {
        for (const float v : {t.mesh.vertices[n].x, t.mesh.vertices[n].y, t.mesh.vertices[n].z,
                              t.mesh.normals[n].x, t.mesh.normals[n].y, t.mesh.normals[n].z}) {
            sum.add(v);
        }
    }
    for (const std::uint32_t i : t.mesh.indices) {
        sum.add(static_cast<std::uint64_t>(i));
    }
    for (const terrain::BlendTexel& b : t.surfaceBlend) {
        sum.add(static_cast<std::uint64_t>(b.ids) << 32 | b.weights);
    }
    for (const float v : t.waterLevels) {
        sum.add(v);
    }
    for (const terrain::WaterColumn& c : t.waterColumns) {
        sum.add(c.level);
        sum.add(c.flowX);
        sum.add(c.flowZ);
        sum.add(static_cast<std::uint64_t>(c.kind));
    }
    for (const float v : t.rock) {
        sum.add(v);
    }
    for (const terrain::Plant& p : t.plants) {
        sum.add(p.x);
        sum.add(p.z);
        sum.add(p.y);
        sum.add(static_cast<std::uint64_t>(p.type));
    }
    sum.add(static_cast<std::uint64_t>(t.falls.size()));
}

std::uint64_t tilesSum(std::size_t& count) {
    Sum sum;
    for (int lod = 0; lod <= 3; ++lod) {
        for (const auto& [x, z] : places()) {
            const terrain::ChunkCoord chunk{
                static_cast<std::int32_t>(std::floor(x / terrain::kChunkSizeMeters)),
                static_cast<std::int32_t>(std::floor(z / terrain::kChunkSizeMeters))};
            addTile(sum, world().tiles->generate({terrain::tileContaining(chunk, lod), 0}));
            ++count;
        }
    }
    return sum.value();
}

std::uint64_t horizonSum(std::size_t& count) {
    terrain::HorizonParams params;
    params.levels = 8;
    params.cellsPerSide = 64;
    params.world = golden::kSmallPlanet;
    const terrain::HorizonSampler horizon(world().tiles->heightfield(), world().tiles->ecology(),
                                          params);
    Sum sum;
    for (int level = 0; level < 6; ++level) {
        for (const auto& [x, z] : places()) {
            const terrain::HorizonIndex c = terrain::canonicalCenter(params, level, x, z);
            for (const terrain::HorizonCell& cell :
                 horizon.sample(level, {c.x - 4, c.z - 4, c.x + 4, c.z + 4})) {
                for (const float v : {cell.height, cell.gradX, cell.gradZ, cell.waterLevel}) {
                    sum.add(v);
                }
                sum.add(static_cast<std::uint64_t>(cell.material) << 16 |
                        static_cast<std::uint64_t>(cell.biome) << 8 | cell.flags);
                sum.add(static_cast<std::uint64_t>(cell.blend.ids) << 32 | cell.blend.weights);
                ++count;
            }
        }
    }
    return sum.value();
}

// Computed by this program on main (ab574e3).
constexpr std::uint64_t kSamples = 0xe4098d6569cf546cULL;
constexpr std::uint64_t kTiles = 0x74e74fd2a21513ddULL;
constexpr std::uint64_t kHorizon = 0x489d869caaa5b039ULL;

} // namespace

int main() {
    std::size_t samples = 0;
    std::size_t ocean = 0;
    std::size_t tiles = 0;
    std::size_t cells = 0;
    const auto start = std::chrono::steady_clock::now();
    const auto lap = [&](const char* what) {
        std::printf("%s at %.1f s\n", what,
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
                        .count());
    };
    (void)world();
    lap("world");
    const std::uint64_t s = samplesSum(samples, ocean);
    lap("samples");
    const std::uint64_t t = tilesSum(tiles);
    lap("tiles");
    const std::uint64_t h = horizonSum(cells);
    lap("horizon");
    std::printf("places %zu; samples %zu (%zu at sea) 0x%016llxULL; tiles %zu 0x%016llxULL; "
                "horizon cells %zu 0x%016llxULL\n",
                places().size(), samples, ocean, static_cast<unsigned long long>(s), tiles,
                static_cast<unsigned long long>(t), cells, static_cast<unsigned long long>(h));
    TEST_CHECK(places().size() == 16 && ocean > samples / 3);
    TEST_CHECK(s == kSamples);
    TEST_CHECK(t == kTiles);
    TEST_CHECK(h == kHorizon);
    return test::result();
}

// The cost of the water (phase 11B) on the small world: the relief with its water against the
// relief alone, in patches (heightsAt over 32 x 32 points 2 m apart) and in tiles (TerrainTiles,
// LOD 0 to 3), on land, in mountains and within 1 km of a river, in its valley. Asserted: at most
// x1.3 on land and in mountains, at most x2 near the rivers. Timed on one thread pair by pair
// (cost_ratio_support.hpp): after a round that builds the courses of the reaches (their cost
// apart), each patch or tile without the water and with it back to back in 5 rounds, the best
// time of each kept on each side; measured again only past the limit. (The small world has
// smaller valleys than the default one, with fewer gorges: its ratios near the rivers are lower.)
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/water/hydrology.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "cost_ratio_support.hpp"
#include "planet_golden_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

namespace {

using terrain::Heightfield;
using terrain::Hydrology;

constexpr std::size_t kPatches = 60;
constexpr std::size_t kTiles = 24;
constexpr int kPatchSide = 32;
constexpr double kPatchStep = 2.0;
constexpr double kNearRiver = 1'000.0;
// The places near the rivers: kPerReach on each reach drawn within kRiverSearch meters of a place
// on land or in mountains.
constexpr std::uint64_t kPerReach = 4;
constexpr double kRiverSearch = 50'000.0;

struct World {
    Heightfield relief;
    std::shared_ptr<const Hydrology> hydrology;
    Heightfield field;
};

World makeWorld() {
    const Heightfield relief(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                             golden::kSmallPlanet);
    const int threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    auto planet = std::make_shared<const terrain::PlanetLevel>(
        terrain::planet::buildPlanet(relief, golden::smallPlanetParams(threads)),
        golden::smallPlanetParams(threads));
    auto hydrology =
        std::make_shared<const Hydrology>(terrain::kDefaultWorldSeed, relief, planet);
    return {relief, hydrology, relief.withHydrology(hydrology)};
}

struct Place {
    double x = 0.0;
    double z = 0.0;
};

struct Kind {
    const char* name = "";
    double most = 0.0; // the highest ratio allowed
    std::vector<Place> at;
};

// The k-th share in [0, 1) of a stream: the same on every platform.
double shareOf(std::uint64_t stream, std::uint64_t k) {
    const std::uint64_t bits =
        terrain::noise::deriveSeed(terrain::noise::deriveSeed(0xC057, stream), k);
    return static_cast<double>(bits >> 11) * 0x1.0p-53;
}

// kPatches places of each kind: on land (no water on the relief), in mountains (their mask above
// one half), and within kNearRiver meters of a point of a reach near a place on land or in
// mountains, where the water carves the relief.
std::array<Kind, 3> placesOf(const World& w) {
    std::array<Kind, 3> kinds{Kind{"land", 1.3, {}}, Kind{"mountains", 1.3, {}},
                              Kind{"within 1 km of a river", 2.0, {}}};
    const double width = golden::kSmallPlanet.x;
    const double height = golden::kSmallPlanet.z;
    for (std::uint64_t k = 0; kinds[0].at.size() < kPatches || kinds[1].at.size() < kPatches;
         ++k) {
        const Place p{(shareOf(0, k) - 0.5) * width, (shareOf(1, k) - 0.5) * height};
        const terrain::HeightSample s = w.relief.sampleAt(p.x, p.z);
        if (s.water != terrain::WaterKind::None) {
            continue;
        }
        if (kinds[0].at.size() < kPatches) {
            kinds[0].at.push_back(p);
        }
        if (s.mountainMask > 0.5f && kinds[1].at.size() < kPatches) {
            kinds[1].at.push_back(p);
        }
    }
    for (std::uint64_t k = 0; kinds[2].at.size() < kPatches; ++k) {
        const Place& from = kinds[k % 2].at[(k / 2) % kPatches];
        const std::vector<std::size_t> near =
            w.hydrology->reachesNear(from.x, from.z, kRiverSearch);
        if (near.empty()) {
            continue;
        }
        const auto pick = [&](std::uint64_t stream, std::uint64_t n, std::size_t count) {
            return std::min(count - 1, static_cast<std::size_t>(shareOf(stream, k * 16 + n) *
                                                                static_cast<double>(count)));
        };
        const terrain::RiverReach reach = w.hydrology->reach(near[pick(2, 0, near.size())]);
        for (std::uint64_t n = 0; n < kPerReach && kinds[2].at.size() < kPatches; ++n) {
            const terrain::RiverPoint& p = reach.points[pick(3, n, reach.points.size())];
            const double angle = 6.283185307179586 * shareOf(4, k * 16 + n);
            const double r = kNearRiver * shareOf(5, k * 16 + n);
            const Place at{terrain::wrapCoordinate(p.x + r * std::cos(angle), width),
                           terrain::wrapCoordinate(p.z + r * std::sin(angle), height)};
            if (!(w.field.sampleAt(at.x, at.z).height <
                  w.relief.sampleAt(at.x, at.z).height - 0.01f)) {
                continue;
            }
            kinds[2].at.push_back(at);
        }
    }
    return kinds;
}

class Stopwatch {
public:
    [[nodiscard]] double seconds() const {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    }

private:
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

// The points and heights of a patch, allocated once.
struct Patch {
    std::vector<double> xs = std::vector<double>(kPatchSide * kPatchSide);
    std::vector<double> zs = std::vector<double>(kPatchSide * kPatchSide);
    std::vector<float> heights = std::vector<float>(kPatchSide * kPatchSide);
};

// The seconds of the patch of a place on field (its height at the center added to sum, so that
// nothing is left out).
double patchSeconds(const Heightfield& field, const Place& p, Patch& patch, double& sum) {
    const double seconds = cost_test::secondsOf([&] {
        for (int j = 0; j < kPatchSide; ++j) {
            for (int i = 0; i < kPatchSide; ++i) {
                patch.xs[static_cast<std::size_t>(j * kPatchSide + i)] = p.x + kPatchStep * i;
                patch.zs[static_cast<std::size_t>(j * kPatchSide + i)] = p.z + kPatchStep * j;
            }
        }
        field.heightsAt(patch.xs, patch.zs, patch.heights);
    });
    sum += static_cast<double>(patch.heights[patch.heights.size() / 2]);
    return seconds;
}

double tileSeconds(const terrain::TerrainTiles& tiles, const terrain::TileRequest& r,
                   double& sum) {
    std::size_t vertices = 0;
    const double seconds =
        cost_test::secondsOf([&] { vertices = tiles.generate(r).mesh.vertices.size(); });
    sum += static_cast<double>(vertices);
    return seconds;
}

// The ratio of the items of a kind with the water over without it, against the limit of the kind.
void check(const char* part, const Kind& k, std::size_t items, const cost_test::TimedRun& run) {
    const cost_test::Ratio ratio = cost_test::ratioAtMost(items, run, k.most);
    std::printf("%-7s %-24s with the water %s\n", part, k.name,
                cost_test::ratioText(ratio).c_str());
    TEST_CHECK(ratio.passed());
}

void testPatches(const World& w, const std::array<Kind, 3>& kinds, double& sum) {
    Patch patch;
    for (const Kind& k : kinds) {
        check("patches", k, k.at.size(), [&](cost_test::Side side, std::size_t n) {
            return patchSeconds(side == cost_test::Side::With ? w.field : w.relief, k.at[n],
                                patch, sum);
        });
    }
}

void testTiles(const World& w, const std::array<Kind, 3>& kinds, double& sum) {
    const terrain::TerrainTiles dry(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                                    terrain::EcologyParams{}, std::nullopt, golden::kSmallPlanet);
    const terrain::TerrainTiles wet(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                                    terrain::EcologyParams{}, std::nullopt, golden::kSmallPlanet,
                                    w.hydrology);
    for (const Kind& k : kinds) {
        std::vector<terrain::TileRequest> at;
        for (std::size_t t = 0; t < kTiles; ++t) {
            const Place& p = k.at[t * k.at.size() / kTiles];
            const terrain::ChunkCoord chunk{
                static_cast<std::int32_t>(std::floor(p.x / terrain::kChunkSizeMeters)),
                static_cast<std::int32_t>(std::floor(p.z / terrain::kChunkSizeMeters))};
            at.push_back({terrain::tileContaining(chunk, static_cast<int>(t % 4)), 0});
        }
        check("tiles", k, at.size(), [&](cost_test::Side side, std::size_t n) {
            return tileSeconds(side == cost_test::Side::With ? wet : dry, at[n], sum);
        });
    }
}

} // namespace

int main() {
    const Stopwatch watch;
    const World w = makeWorld();
    const std::array<Kind, 3> kinds = placesOf(w);
    std::printf("small world and places: %.1f s\n", watch.seconds());
    double sum = 0.0;
    testPatches(w, kinds, sum);
    testTiles(w, kinds, sum);
    TEST_CHECK(std::isfinite(sum));
    std::printf("total: %.1f s\n", watch.seconds());
    return test::result();
}

// The world without any experimental flag (experimental_flags.hpp) is the world of main, bit for
// bit, however many rows the table of the flags holds (agent 51: the relief of the wave merged
// switched off): every field of the samples of the small world with its planet level and its
// water, built from the parameters of an empty text of flags, at sea (a lattice of 16 km, the
// land every 128 km) and on the coasts (windows of 9.6 km at 150 m). played_world_golden_test
// (agent 42) pins the tiles and the horizon of the world as the game plays it at five places;
// this test pins the sea and the coasts, where most flags of the wave act.
//
// Pinned by « Experimental flags: one text switches the relief of the wave on » (agent 51): every
// part, created on main 8cfd1cf by this program, noFlags() returning the default parameters.

#include "dunes_support.hpp"
#include "golden_support.hpp"
#include "test_assert.hpp"

#include "terrain/generation/experimental_flags.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

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

// The parameters of the world without any flag.
terrain::WorldParams noFlags() {
    terrain::WorldParams p;
    const terrain::AppliedFlags applied = terrain::applyExperimentalFlags("", p);
    TEST_CHECK(applied.text.empty() && applied.unknown.empty());
    return p;
}

const terrain::Heightfield& field() {
    static const std::unique_ptr<terrain::TerrainTiles> tiles = [] {
        terrain::WorldParams p = noFlags();
        p.water.ergs = dunes_test::smallErgParams();
        auto hydrology = std::make_shared<const terrain::Hydrology>(
            dunes_test::kSeed, dunes_test::small().relief, dunes_test::small().planet, p.water);
        return std::make_unique<terrain::TerrainTiles>(dunes_test::kSeed, p.relief, p.ecology,
                                                       std::nullopt, golden::kSmallPlanet,
                                                       hydrology);
    }();
    return tiles->heightfield();
}

double coastAt(double x, double z) {
    return static_cast<double>(dunes_test::small().relief.sampleAt(x, z).continentalness) *
           kContinentalScale;
}

// Places of a lattice of 64 km on the coasts (the band of agent 33 and the shore), spread.
std::vector<Place> coasts() {
    std::vector<Place> all;
    const terrain::WorldSize& w = golden::kSmallPlanet;
    for (double z = -0.5 * w.z; z < 0.5 * w.z; z += 65'536.0) {
        for (double x = -0.5 * w.x; x < 0.5 * w.x; x += 65'536.0) {
            const double coast = coastAt(x, z);
            if (coast >= -25'000.0 && coast < 5'000.0) {
                all.emplace_back(x + 1'234.5, z + 2'345.5);
            }
        }
    }
    std::vector<Place> out;
    const std::size_t stride = std::max<std::size_t>(1, all.size() / 8);
    for (std::size_t k = stride / 2; k < all.size() && out.size() < 8; k += stride) {
        out.push_back(all[k]);
    }
    return out;
}

void addSample(golden::Checksum& sum, const terrain::HeightSample& s) {
    for (const float v : {s.continentalness, s.macroContinentalness, s.plateContinentalness,
                          s.plateCollision, s.mountainMask, s.relief, s.rift, s.base, s.regional,
                          s.mountain, s.detail, s.height, s.waterLevel, s.dunes, s.erg,
                          s.ergFloor, s.rock}) {
        sum.add(v);
    }
    sum.add(static_cast<std::uint64_t>(s.water), 1);
}

// One by one (a batch spread over the world would gather the water of all of it): every 16 km at
// sea, every 128 km on land (the courses of all the rivers of the world otherwise).
std::uint64_t seaSum(std::size_t& count, std::size_t& ocean) {
    golden::Checksum sum;
    const terrain::WorldSize& w = golden::kSmallPlanet;
    for (double z = -0.5 * w.z + 8'192.0; z < 0.5 * w.z; z += 16'384.0) {
        for (double x = -0.5 * w.x + 4'096.0; x < 0.5 * w.x; x += 16'384.0) {
            const bool node = std::fmod(x + 0.5 * w.x - 4'096.0, 131'072.0) == 0.0 &&
                              std::fmod(z + 0.5 * w.z - 8'192.0, 131'072.0) == 0.0;
            if (!node && coastAt(x, z) >= 0.0) {
                continue;
            }
            const terrain::HeightSample s = field().sampleAt(x, z);
            addSample(sum, s);
            ocean += s.water == terrain::WaterKind::Ocean ? 1 : 0;
            ++count;
        }
    }
    return sum.value();
}

// Windows of 64 x 64 samples 150 m apart, a batch per row.
std::uint64_t coastSum(std::size_t& count, std::size_t& places) {
    golden::Checksum sum;
    const terrain::WorldSize& w = golden::kSmallPlanet;
    for (const auto& [x, z] : coasts()) {
        ++places;
        for (int j = 0; j < 64; ++j) {
            std::vector<double> xs;
            for (int i = 0; i < 64; ++i) {
                xs.push_back(terrain::wrapCoordinate(x + 150.0 * (i - 32), w.x));
            }
            std::vector<double> zs(xs.size(), terrain::wrapCoordinate(z + 150.0 * (j - 32), w.z));
            std::vector<terrain::HeightSample> out(xs.size());
            field().samplesAt(xs, zs, out);
            for (const terrain::HeightSample& s : out) {
                addSample(sum, s);
            }
            count += out.size();
        }
    }
    return sum.value();
}

} // namespace

int main() {
    std::size_t sea = 0;
    std::size_t ocean = 0;
    std::size_t coast = 0;
    std::size_t places = 0;
    const std::uint64_t seaPart = seaSum(sea, ocean);
    const std::uint64_t coastPart = coastSum(coast, places);
    std::printf("%zu samples at sea (%zu of the ocean), %zu on the coasts at %zu places\n", sea,
                ocean, coast, places);
    TEST_CHECK(ocean > sea * 9 / 10 && places == 8);
    golden::report("the world without any experimental flag",
                   {{"samples at sea", seaPart, 0xF156D1420B94851FULL},
                    {"samples of the coasts", coastPart, 0x29468A26A6D3ECA4ULL}});
    return test::result();
}

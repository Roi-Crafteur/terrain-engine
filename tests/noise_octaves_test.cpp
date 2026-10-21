// FbmOctaves gives exactly the bits of fbm2D, ridged2D and fbm3D with the same seed, parameters
// and world, for any parameters: no octave or a negative count, more octaves than rotations,
// another lacunarity, a negative or zero gain, a total amplitude of 0; on pseudo-random points,
// on the edges and the corner of the world, at -0, and far beyond it.
#include "terrain/generation/noise.hpp"
#include "terrain/generation/noise_octaves.hpp"

#include "bit_exact_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace {

namespace noise = terrain::noise;
using bit_exact::Point;
using bit_exact::same;
using bit_exact::Tally;
using terrain::WorldSize;

constexpr std::uint64_t kSeed = 4242;

const std::array<noise::FbmParams, 10> kParams = {{
    {.octaves = -1, .frequency = 1.0 / 900.0},
    {.octaves = 0, .frequency = 1.0 / 900.0},
    {.octaves = 1, .frequency = 1.0 / 60.0},
    {.octaves = 8, .frequency = 1.0 / 30000.0},
    {.octaves = 9, .frequency = 1.0 / 5000.0, .lacunarity = 1.9, .gain = 0.55},
    {.octaves = 12, .frequency = 1.0 / 2e6, .lacunarity = 2.5, .gain = 0.45},
    {.octaves = 3, .frequency = 1.0 / 700.0, .gain = -0.4},
    {.octaves = 4, .frequency = 1.0 / 700.0, .gain = 0.0},
    {.octaves = 2, .frequency = 1.0 / 700.0, .gain = -1.0}, // total amplitude 0
    {.octaves = 4, .frequency = 1.0 / 60000.0},
}};

void checkPoint(Tally& tally, const noise::FbmParams& params, const WorldSize& world,
                const noise::FbmOctaves& octaves, Point p) {
    const double y = 0.29 * p.z - 0.53 * p.x;
    tally.check(same(octaves.fbm2D(p.x, p.z), noise::fbm2D(p.x, p.z, kSeed, params, world)), p,
                "fbm2D");
    for (const double sharpness : {1.0, 2.0, 2.5, 4.0}) {
        tally.check(same(octaves.ridged2D(p.x, p.z, sharpness),
                         noise::ridged2D(p.x, p.z, kSeed, params, world, sharpness)),
                    p, "ridged2D");
    }
    tally.check(same(octaves.fbm3D(p.x, y, p.z), noise::fbm3D(p.x, y, p.z, kSeed, params, world)),
                p, "fbm3D");
}

void checkFreeFunctions() {
    Tally tally("FbmOctaves == free functions");
    const std::vector<Point> random = bit_exact::randomPoints(5'000, 4e7);
    const std::vector<Point> near = bit_exact::randomPoints(2'000, 500.0);
    for (const WorldSize& world : bit_exact::kWorlds) {
        const std::vector<Point> edges = bit_exact::edgePoints(world);
        for (const noise::FbmParams& params : kParams) {
            const noise::FbmOctaves octaves(kSeed, params, world);
            TEST_CHECK(octaves.octaves() == std::max(params.octaves, 0));
            for (const auto* set : {&random, &near, &edges}) {
                for (const Point& p : *set) {
                    checkPoint(tally, params, world, octaves, p);
                }
            }
        }
    }
}

void checkEmpty() {
    const noise::FbmOctaves none;
    TEST_CHECK(none.octaves() == 0);
    TEST_CHECK(same(none.fbm2D(12.5, -3.0), 0.0f));
    TEST_CHECK(same(none.ridged2D(12.5, -3.0, 2.0), 0.0f));
    TEST_CHECK(same(none.fbm3D(12.5, 7.0, -3.0), 0.0f));
    // A total amplitude of 0: the free functions give +0 too.
    const noise::FbmParams cancelled{.octaves = 2, .frequency = 0.01, .gain = -1.0};
    const noise::FbmOctaves zero(kSeed, cancelled, WorldSize{});
    TEST_CHECK(same(zero.fbm2D(40.0, 2.0), 0.0f));
    TEST_CHECK(same(noise::fbm2D(40.0, 2.0, kSeed, cancelled, WorldSize{}), 0.0f));
}

} // namespace

int main() {
    checkFreeFunctions();
    checkEmpty();
    return test::result();
}

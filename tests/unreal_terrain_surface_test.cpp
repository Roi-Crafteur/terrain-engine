// What the temporary terrain material reads (Bridge/terrain_surface): the surface of a sample
// packed exactly into a float of the horizon texels, and the fade of the drop of the horizon
// across the band of tiles over level 0, which leaves no step at the outer edge of the tiles.
#include "terrain_surface.hpp"

#include "test_assert.hpp"

#include <limits>

namespace {

using terrain::Biome;
using terrain::SurfaceMaterial;
using terrain::unreal::SurfaceCode;

void testSurfaceCodes() {
    bool roundTrip = true;
    bool exact = true;
    for (int b = 0; b < terrain::kBiomeCount; ++b) {
        for (int m = 0; m < terrain::kSurfaceMaterialCount; ++m) {
            const auto material = static_cast<SurfaceMaterial>(m);
            const auto biome = static_cast<Biome>(b);
            const float code = terrain::unreal::packSurface(material, biome);
            roundTrip = roundTrip && terrain::unreal::unpackSurface(code) == SurfaceCode{material, biome};
            // The integer the materials decode: material + 8 biome, never negative.
            exact = exact && code == static_cast<float>(m + terrain::unreal::kSurfaceBiomeStride * b);
            // Under a lake or a river: the water code on top.
            const float wet = terrain::unreal::packSurface(material, biome, true);
            roundTrip = roundTrip &&
                        terrain::unreal::unpackSurface(wet) == SurfaceCode{material, biome, true};
            exact = exact && wet == code + static_cast<float>(terrain::unreal::kSurfaceWaterCode);
        }
    }
    TEST_CHECK(roundTrip);
    TEST_CHECK(exact);
    TEST_CHECK(terrain::unreal::packSurface(SurfaceMaterial::Water, Biome::Ocean) == 0.0f);
    // Anything else (the negative ground of the ocean, NaN, too large) is the ocean.
    for (const float bad : {-3.0f, 96.0f, 224.0f, 256.0f, 1.0e6f,
                            std::numeric_limits<float>::quiet_NaN()}) {
        TEST_CHECK(terrain::unreal::unpackSurface(bad) == SurfaceCode{});
    }
}

void testDropFade() {
    // Level 0 of 8 m, a hole of 40 cells (320 m) and 128 m of tiles around it.
    const terrain::unreal::TileHole hole{40, 128.0};
    const terrain::unreal::DropFade fade = terrain::unreal::dropFade(hole, 8.0);
    TEST_CHECK(fade.coveredMeters == 448.0 && fade.fadeMeters == 64.0);
    TEST_CHECK(terrain::unreal::dropShare(fade, 320.0) == 1.0);  // the edge of the hole
    TEST_CHECK(terrain::unreal::dropShare(fade, 384.0) == 1.0);  // the inner half of the band
    TEST_CHECK(terrain::unreal::dropShare(fade, 416.0) == 0.5);
    TEST_CHECK(terrain::unreal::dropShare(fade, 448.0) == 0.0);  // the edge of the tiles
    TEST_CHECK(terrain::unreal::dropShare(fade, 1024.0) == 0.0); // the border of level 0, beyond
    double previous = 1.0;
    bool decreasing = true;
    for (double d = 300.0; d < 500.0; d += 1.0) {
        const double share = terrain::unreal::dropShare(fade, d);
        decreasing = decreasing && share <= previous && share >= 0.0 && share <= 1.0;
        previous = share;
    }
    TEST_CHECK(decreasing);
    // No band: a step at the edge.
    const terrain::unreal::DropFade sharp{100.0, 0.0};
    TEST_CHECK(terrain::unreal::dropShare(sharp, 99.0) == 1.0);
    TEST_CHECK(terrain::unreal::dropShare(sharp, 100.0) == 0.0);
}

} // namespace

int main() {
    testSurfaceCodes();
    testDropFade();
    return test::result();
}

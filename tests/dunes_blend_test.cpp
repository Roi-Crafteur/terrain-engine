// The share of the dunes of the ergs (SurfaceBlend::dunes, agent 34) on its way to the horizon
// (agent 29): packed in 127ths in bits 15-21 of the materials of a texel (agent 49), read back
// exactly; summed with the weights (addBlend) and averaged over the footprint of a sample
// (footprintBlend); taken from the relief by the site of a sample (pointSite, as
// surfaceFromHeightfield); dropped where the edits of the player cover the ground (withMaterial).
#include "horizon_blend.hpp"

#include "test_assert.hpp"

#include <cmath>
#include <cstdio>

namespace {

using terrain::SurfaceBlend;

SurfaceBlend desertSand(float dunes) {
    SurfaceBlend b;
    b.biomes = {terrain::Biome::SandDesert, terrain::Biome::SandDesert, terrain::Biome::SandDesert};
    b.biomeWeights = {1.0f, 0.0f, 0.0f};
    b.materials = {terrain::SurfaceMaterial::Sand, terrain::SurfaceMaterial::Sand,
                   terrain::SurfaceMaterial::Sand};
    b.materialWeights = {1.0f, 0.0f, 0.0f};
    b.dunes = dunes;
    return b;
}

void testPacking() {
    bool exact = true;
    for (int units = 0; units <= 127; ++units) {
        const float share = static_cast<float>(units) / 127.0f;
        const terrain::BlendTexel t = terrain::packBlend(desertSand(share));
        exact = exact && (t.materials >> terrain::kBlendDuneShift) == static_cast<unsigned>(units) &&
                terrain::unpackBlend(t).dunes == share;
    }
    TEST_CHECK(exact);
    // Off the ergs, the ids are those of a blend without dunes.
    TEST_CHECK((terrain::packBlend(desertSand(0.0f)).materials >> terrain::kBlendDuneShift) == 0U);
}

void testFootprint() {
    const SurfaceBlend dune = desertSand(1.0f);
    const SurfaceBlend floor = desertSand(0.0f);
    std::array<const SurfaceBlend*, 9> around{};
    around.fill(&floor);
    around[4] = &dune; // the sample itself: 9/16 of its footprint
    const float mean = terrain::horizon::footprintBlend(around).dunes;
    std::printf("footprint: one dune sample among floors, share %.4f\n", mean);
    TEST_CHECK(std::abs(mean - 9.0f / 16.0f) < 1e-6f);
    around.fill(&dune);
    TEST_CHECK(terrain::horizon::footprintBlend(around).dunes == 1.0f);
}

void testSiteAndEdits() {
    terrain::HeightSample s;
    s.height = 200.0f;
    s.dunes = 42.0f;
    const terrain::SurfaceSite site =
        terrain::horizon::pointSite(s, {200.0f, 200.0f, 200.0f, 200.0f}, 8.0);
    TEST_CHECK(site.dunes == 42.0f);
    const terrain::BlendTexel edited = terrain::horizon::withMaterial(
        terrain::packBlend(desertSand(1.0f)), terrain::SurfaceMaterial::Rock);
    TEST_CHECK(terrain::unpackBlend(edited).dunes == 0.0f);
}

} // namespace

int main() {
    testPacking();
    testFootprint();
    testSiteAndEdits();
    return test::result();
}

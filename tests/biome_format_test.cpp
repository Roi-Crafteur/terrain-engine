// The format of the blend texels (agent 49, biome_blend.hpp): every field of a SurfaceBlend comes
// back from its texel and never spills into another one.
// - every biome id (64) and material id (32) in every slot, with the others at their largest;
// - every kind of water (16 places, 5 kinds today), every kind of cover (4) and every share of
//   the cover (0 to 255) and of the dunes (0 to 127), together and apart;
// - every field below 2^24 (an exact integer in a float), bit 23 of the biomes left to the readers,
//   the byte kept for the heat of the ground at 0;
// - addTexel of a texel equals addBlend of its blend.
#include "terrain/generation/biome_blend.hpp"

#include "test_assert.hpp"

#include <cstddef>
#include <cstdint>

namespace {

using terrain::BlendTexel;
using terrain::SurfaceBlend;

constexpr std::uint32_t kField = 1U << terrain::kBlendFieldBits;

// A blend whose slots hold these ids, the weights 0.5, 0.3, 0.2.
SurfaceBlend blendOf(std::uint32_t biome, std::uint32_t material) {
    SurfaceBlend b;
    for (std::size_t s = 0; s < terrain::kBlendSlots; ++s) {
        b.biomes[s] = static_cast<terrain::Biome>(biome);
        b.materials[s] = static_cast<terrain::SurfaceMaterial>(material);
    }
    b.biomeWeights = {0.5f, 0.3f, 0.2f};
    b.materialWeights = {0.5f, 0.3f, 0.2f};
    return b;
}

bool sameExceptWeights(const SurfaceBlend& a, const SurfaceBlend& b) {
    return a.biomes == b.biomes && a.materials == b.materials && a.water == b.water &&
           a.coverKind == b.coverKind && a.dunes == b.dunes && a.cover == b.cover;
}

bool fits(const BlendTexel& t) {
    return t.biomes < (1U << terrain::kBlendPresentBit) && t.materials < kField &&
           t.biomeWeights < kField && t.materialWeights < (1U << 16U);
}

// Every id in every slot, the other slots at the largest id: nothing spills between slots, into
// the water, the dunes or the cover.
void testIds() {
    bool ok = true;
    const std::uint32_t biomes = 1U << terrain::kBiomeIdBits;
    const std::uint32_t materials = 1U << terrain::kMaterialIdBits;
    for (std::uint32_t id = 0; id < biomes; ++id) {
        for (std::size_t s = 0; s < terrain::kBlendSlots; ++s) {
            SurfaceBlend b = blendOf(biomes - 1, materials - 1);
            b.biomes[s] = static_cast<terrain::Biome>(id);
            b.materials[s] = static_cast<terrain::SurfaceMaterial>(id % materials);
            b.water = terrain::WaterKind::SaltLake;
            b.dunes = 1.0f;
            b.cover = 1.0f;
            b.coverKind = terrain::CoverKind::Salt;
            const BlendTexel t = terrain::packBlend(b);
            ok = ok && fits(t) && sameExceptWeights(terrain::unpackBlend(t), b);
        }
    }
    TEST_CHECK(ok);
}

// Every water, cover kind, cover share and dune share, beside ids at their largest and at 0.
void testWaterCoverDunes() {
    bool ok = true;
    for (const std::uint32_t id : {0U, 63U}) {
        for (std::uint32_t water = 0; water < 16; ++water) {
            for (std::uint32_t kind = 0; kind < 4; ++kind) {
                for (std::uint32_t units = 0; units <= 255; units += 17) {
                    SurfaceBlend b = blendOf(id, id % 32);
                    b.water = static_cast<terrain::WaterKind>(water);
                    b.coverKind = static_cast<terrain::CoverKind>(kind);
                    b.cover = static_cast<float>(units) / 255.0f;
                    b.dunes = static_cast<float>(units % 128) / 127.0f;
                    const BlendTexel t = terrain::packBlend(b);
                    ok = ok && fits(t) && sameExceptWeights(terrain::unpackBlend(t), b);
                }
            }
        }
    }
    TEST_CHECK(ok);
    // The positions the shaders read (Scripts/blend_layers_hlsl.py checks them against these).
    SurfaceBlend b = blendOf(0, 0);
    b.water = terrain::WaterKind::Lake;
    b.coverKind = terrain::CoverKind::Snow;
    b.cover = 1.0f;
    const BlendTexel t = terrain::packBlend(b);
    TEST_CHECK(((t.biomes >> 18U) & 15U) == 3U);
    TEST_CHECK(((t.materials >> 22U) & 3U) == 1U);
    TEST_CHECK(((t.biomeWeights >> 16U) & 255U) == 255U);
    TEST_CHECK((t.materialWeights >> 16U) == 0U); // the heat of the ground: none yet
}

void testAddTexel() {
    SurfaceBlend b = blendOf(5, 2);
    b.biomes[1] = terrain::Biome::Savanna;
    b.materials[2] = terrain::SurfaceMaterial::Dirt;
    b.dunes = 0.5f;
    b.cover = 0.25f;
    terrain::BlendWeights fromTexel;
    terrain::BlendWeights fromBlend;
    terrain::addTexel(fromTexel, terrain::packBlend(b), 0.75f);
    terrain::addBlend(fromBlend, terrain::unpackBlend(terrain::packBlend(b)), 0.75f);
    TEST_CHECK(fromTexel.biomes == fromBlend.biomes && fromTexel.materials == fromBlend.materials);
    TEST_CHECK(fromTexel.dunes == fromBlend.dunes && fromTexel.cover == fromBlend.cover);
    TEST_CHECK(fromTexel.cover > 0.18f && fromTexel.cover < 0.19f);
}

} // namespace

int main() {
    testIds();
    testWaterCoverDunes();
    testAddTexel();
    return test::result();
}

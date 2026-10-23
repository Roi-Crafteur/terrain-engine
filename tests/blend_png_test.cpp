// Maps of the transitions: the weights of the dominant ids alone and of the whole blend, the
// colors of the biome and material maps mixed by the weights, the size of the images.

#include "terrain/io/blend_png.hpp"

#include "test_assert.hpp"

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace {

using terrain::Biome;
using terrain::BlendWeights;
using terrain::SurfaceBlend;
using terrain::SurfaceMaterial;

SurfaceBlend forestEdge() {
    SurfaceBlend blend;
    blend.biomes = {Biome::Grassland, Biome::TemperateForest, Biome::Grassland};
    blend.biomeWeights = {0.75f, 0.25f, 0.0f};
    blend.materials = {SurfaceMaterial::Grass, SurfaceMaterial::Dirt, SurfaceMaterial::Grass};
    blend.materialWeights = {0.5f, 0.5f, 0.0f};
    return blend;
}

void testWeights() {
    const SurfaceBlend blend = forestEdge();
    const BlendWeights dominant = terrain::dominantWeights(blend);
    TEST_CHECK(dominant.biomes[static_cast<std::size_t>(Biome::Grassland)] == 1.0f);
    TEST_CHECK(dominant.biomes[static_cast<std::size_t>(Biome::TemperateForest)] == 0.0f);
    TEST_CHECK(dominant.materials[static_cast<std::size_t>(SurfaceMaterial::Grass)] == 1.0f);
    const BlendWeights all = terrain::blendWeights(blend);
    TEST_CHECK(all.biomes[static_cast<std::size_t>(Biome::Grassland)] == 0.75f);
    TEST_CHECK(all.biomes[static_cast<std::size_t>(Biome::TemperateForest)] == 0.25f);
    TEST_CHECK(all.materials[static_cast<std::size_t>(SurfaceMaterial::Dirt)] == 0.5f);
}

void testColors() {
    const SurfaceBlend blend = forestEdge();
    TEST_CHECK(terrain::mixedBiomeColor(terrain::dominantWeights(blend)) ==
               terrain::kBiomeRgb[static_cast<std::size_t>(Biome::Grassland)]);
    const terrain::Rgb8 mixed = terrain::mixedMaterialColor(terrain::blendWeights(blend));
    const terrain::Rgb8 grass =
        terrain::kMaterialRgb[static_cast<std::size_t>(SurfaceMaterial::Grass)];
    const terrain::Rgb8 dirt =
        terrain::kMaterialRgb[static_cast<std::size_t>(SurfaceMaterial::Dirt)];
    for (std::size_t c = 0; c < mixed.size(); ++c) {
        const int expected = (grass[c] + dirt[c] + 1) / 2;
        TEST_CHECK(mixed[c] == expected || mixed[c] + 1 == expected);
    }
}

void testImages() {
    const std::vector<BlendWeights> weights(6, terrain::blendWeights(forestEdge()));
    const terrain::Image biomes = terrain::renderMixedBiomeMap(weights, 3, 2);
    TEST_CHECK(biomes.width == 3 && biomes.height == 2 && biomes.channels == 3);
    TEST_CHECK(biomes.pixels.size() == 18);
    const terrain::Image materials = terrain::renderMixedMaterialMap(weights, 2, 3);
    TEST_CHECK(materials.width == 2 && materials.height == 3 && materials.pixels.size() == 18);
    bool threw = false;
    try {
        (void)terrain::renderMixedBiomeMap(weights, 4, 2);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    TEST_CHECK(threw);
}

} // namespace

int main() {
    testWeights();
    testColors();
    testImages();
    return test::result();
}

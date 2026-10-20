// Biome, surface material and vegetation density maps: exact palettes on a synthetic map, and
// the three files written by the export.
#include "terrain/generation/ecology.hpp"
#include "terrain/io/ecology_png.hpp"
#include "terrain/io/png.hpp"

#include "test_assert.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

using terrain::EcologyMap;
using terrain::EcologySample;
using terrain::Image;
using terrain::PlantType;
using terrain::Rgb8;
using terrain::SurfaceMaterial;

Rgb8 pixel(const Image& image, int column, int row) {
    const auto offset = static_cast<std::size_t>((row * image.width + column) * 3);
    return {image.pixels[offset], image.pixels[offset + 1], image.pixels[offset + 2]};
}

// 4 x 4 map: sample i has biome i % 12 and material i % 8; the first row is land with total
// densities 0, 1/3, 2/3 and 1, the second row water with the same seaweed densities.
EcologyMap makeMap() {
    EcologyMap map{{.metersPerPixel = 10.0, .size = 4}, {}};
    for (int i = 0; i < 16; ++i) {
        EcologySample sample;
        sample.biome = static_cast<terrain::Biome>(i % terrain::kBiomeCount);
        sample.material = static_cast<SurfaceMaterial>(i % terrain::kSurfaceMaterialCount);
        const float density = static_cast<float>(i % 4) / 3.0f;
        if (i / 4 == 0) {
            sample.material = SurfaceMaterial::Grass;
            sample.densities[static_cast<std::size_t>(PlantType::Tree)] = 0.5f * density;
            sample.densities[static_cast<std::size_t>(PlantType::TallGrass)] = 0.5f * density;
        } else if (i / 4 == 1) {
            sample.material = SurfaceMaterial::Water;
            sample.densities[static_cast<std::size_t>(PlantType::Seaweed)] = density;
        }
        map.samples.push_back(sample);
    }
    return map;
}

void testPalettesDistinct() {
    for (std::size_t a = 0; a < terrain::kBiomeRgb.size(); ++a) {
        for (std::size_t b = 0; b < a; ++b) {
            TEST_CHECK(terrain::kBiomeRgb[a] != terrain::kBiomeRgb[b]);
        }
    }
    for (std::size_t a = 0; a < terrain::kMaterialRgb.size(); ++a) {
        for (std::size_t b = 0; b < a; ++b) {
            TEST_CHECK(terrain::kMaterialRgb[a] != terrain::kMaterialRgb[b]);
        }
    }
}

void testBiomeAndMaterialMaps() {
    const EcologyMap map = makeMap();
    const Image biomes = terrain::renderBiomeMap(map);
    const Image materials = terrain::renderSurfaceMaterialMap(map);
    TEST_CHECK(biomes.width == 4 && biomes.height == 4 && biomes.channels == 3);
    TEST_CHECK(biomes.pixels.size() == 4 * 4 * 3 && materials.pixels.size() == 4 * 4 * 3);
    bool exact = true;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            const EcologySample& sample = map.at(column, row);
            exact = exact &&
                    pixel(biomes, column, row) ==
                        terrain::kBiomeRgb[static_cast<std::size_t>(sample.biome)] &&
                    pixel(materials, column, row) ==
                        terrain::kMaterialRgb[static_cast<std::size_t>(sample.material)];
        }
    }
    TEST_CHECK(exact);
}

void testVegetationDensityMap() {
    const EcologyMap map = makeMap();
    const Image image = terrain::renderVegetationDensityMap(map);
    TEST_CHECK(image.width == 4 && image.height == 4 && image.channels == 3);
    TEST_CHECK(pixel(image, 0, 0) == terrain::kBareGroundRgb);
    TEST_CHECK(pixel(image, 3, 0) == terrain::kDenseVegetationRgb);
    TEST_CHECK(pixel(image, 0, 1) == terrain::kOpenWaterRgb);
    TEST_CHECK(pixel(image, 3, 1) == terrain::kDenseSeaweedRgb);
    // Denser vegetation is greener, denser seaweed is lighter.
    TEST_CHECK(pixel(image, 1, 0)[1] < pixel(image, 2, 0)[1]);
    TEST_CHECK(pixel(image, 1, 1)[1] < pixel(image, 2, 1)[1]);
    // The rows below hold no density: bare ground or open water, by material.
    for (int column = 0; column < 4; ++column) {
        const bool wet = map.at(column, 2).material == SurfaceMaterial::Water;
        TEST_CHECK(pixel(image, column, 2) ==
                   (wet ? terrain::kOpenWaterRgb : terrain::kBareGroundRgb));
    }
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void testExportWritesThreeImages() {
    const terrain::Heightfield field(terrain::kDefaultWorldSeed);
    const terrain::HeightmapRegion region{.metersPerPixel = 1000.0, .size = 32};
    const terrain::Ecology ecology(terrain::kDefaultWorldSeed);
    const EcologyMap map = ecology.map(terrain::surfaceFromHeightfield(field, region));
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "terrain_ecology_png_test";
    std::filesystem::remove_all(directory);
    TEST_CHECK(terrain::exportEcologyPngs(map, directory));
    TEST_CHECK(readFile(directory / "biome_map.png") ==
               terrain::encodePng(terrain::renderBiomeMap(map)));
    TEST_CHECK(readFile(directory / "surface_material_map.png") ==
               terrain::encodePng(terrain::renderSurfaceMaterialMap(map)));
    TEST_CHECK(readFile(directory / "vegetation_density_map.png") ==
               terrain::encodePng(terrain::renderVegetationDensityMap(map)));
    std::filesystem::remove_all(directory);
}

} // namespace

int main() {
    testPalettesDistinct();
    testBiomeAndMaterialMaps();
    testVegetationDensityMap();
    testExportWritesThreeImages();
    return test::result();
}

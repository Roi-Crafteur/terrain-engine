#include "terrain/generation/heightfield.hpp"
#include "terrain/io/heightmap_png.hpp"
#include "terrain/io/png.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

using terrain::HeightGrid;
using terrain::Heightfield;
using terrain::HeightmapRegion;
using terrain::Image;

void testGridLayout() {
    const Heightfield field(terrain::kDefaultWorldSeed);
    const HeightmapRegion region{.centerX = 100.0, .centerZ = -50.0, .metersPerPixel = 10.0,
                                 .size = 8};
    const HeightGrid grid = terrain::sampleHeightGrid(field, region);
    TEST_CHECK(grid.size == 8);
    TEST_CHECK(grid.heights.size() == 64);
    // Pixel centers, +X to the right and +Z downward.
    TEST_CHECK(grid.at(0, 0) == field.heightAt(100.0 - 35.0, -50.0 - 35.0));
    TEST_CHECK(grid.at(7, 0) == field.heightAt(100.0 + 35.0, -50.0 - 35.0));
    TEST_CHECK(grid.at(2, 5) == field.heightAt(100.0 - 15.0, -50.0 + 15.0));
}

void testGrayLevels() {
    TEST_CHECK(terrain::grayLevel(terrain::kHeightmapMinHeight) == 0);
    TEST_CHECK(terrain::grayLevel(terrain::kHeightmapMinHeight - 100.0f) == 0);
    TEST_CHECK(terrain::grayLevel(terrain::kHeightmapMaxHeight) == 255);
    TEST_CHECK(terrain::grayLevel(terrain::kHeightmapMaxHeight + 100.0f) == 255);
    TEST_CHECK(terrain::grayLevel(0.0f) < terrain::grayLevel(1000.0f));

    const HeightGrid grid{2, 1.0, {-200.0f, 0.0f, 900.0f, 2500.0f}};
    const Image image = terrain::renderHeightmapGray(grid);
    TEST_CHECK(image.width == 2 && image.height == 2 && image.channels == 1);
    bool matches = image.pixels.size() == 4;
    for (std::size_t i = 0; matches && i < 4; ++i) {
        matches = image.pixels[i] == terrain::grayLevel(grid.heights[i]);
    }
    TEST_CHECK(matches);
}

// Water is drawn blue, land never is.
void testPreviewColors() {
    const Heightfield field(terrain::kDefaultWorldSeed);
    const HeightmapRegion region{.metersPerPixel = 2000.0, .size = 64}; // 128 km
    const HeightGrid grid = terrain::sampleHeightGrid(field, region);
    const Image image = terrain::renderHeightmapPreview(grid);
    TEST_CHECK(image.width == 64 && image.height == 64 && image.channels == 3);
    TEST_CHECK(image.pixels.size() == grid.heights.size() * 3);

    int water = 0;
    bool colorsMatch = true;
    for (std::size_t i = 0; i < grid.heights.size(); ++i) {
        const std::uint8_t r = image.pixels[3 * i];
        const std::uint8_t g = image.pixels[3 * i + 1];
        const std::uint8_t b = image.pixels[3 * i + 2];
        const bool blue = b > r && b > g;
        const bool isWater = grid.heights[i] < terrain::kSeaLevel;
        colorsMatch = colorsMatch && blue == isWater;
        water += isWater ? 1 : 0;
    }
    TEST_CHECK(colorsMatch);
    TEST_CHECK(water > 0 && water < static_cast<int>(grid.heights.size()));
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void testExportWritesBothImages() {
    const Heightfield field(terrain::kDefaultWorldSeed);
    const HeightmapRegion region{.metersPerPixel = 500.0, .size = 32};
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "terrain_heightmap_png_test";
    std::filesystem::remove_all(directory);
    TEST_CHECK(terrain::exportHeightmapPngs(field, region, directory));

    const HeightGrid grid = terrain::sampleHeightGrid(field, region);
    TEST_CHECK(readFile(directory / "heightmap.png") ==
               terrain::encodePng(terrain::renderHeightmapGray(grid)));
    TEST_CHECK(readFile(directory / "heightmap_preview.png") ==
               terrain::encodePng(terrain::renderHeightmapPreview(grid)));
    std::filesystem::remove_all(directory);
}

} // namespace

int main() {
    testGridLayout();
    testGrayLevels();
    testPreviewColors();
    testExportWritesBothImages();
    return test::result();
}

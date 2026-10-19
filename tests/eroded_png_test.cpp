#include "terrain/generation/eroded_region.hpp"
#include "terrain/io/eroded_png.hpp"
#include "terrain/io/heightmap_png.hpp"
#include "terrain/io/png.hpp"

#include "test_assert.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

using terrain::Cascade;
using terrain::HeightGrid;
using terrain::Image;
using terrain::RiverNetwork;

struct Rgb {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
};

Rgb pixel(const Image& image, int column, int row) {
    const auto offset = static_cast<std::size_t>((row * image.width + column) * 3);
    return {image.pixels[offset], image.pixels[offset + 1], image.pixels[offset + 2]};
}

bool hasColor(Rgb color, const std::uint8_t (&rgb)[3]) {
    return color.r == rgb[0] && color.g == rgb[1] && color.b == rgb[2];
}

// 8 x 8 land grid with an ocean column, a river along row 4 and a cascade at (5, 4).
struct Scene {
    HeightGrid grid;
    RiverNetwork rivers;
};

Scene makeScene() {
    constexpr int kSize = 8;
    Scene scene;
    scene.grid = HeightGrid{kSize, 100.0, {}};
    const auto cells = static_cast<std::size_t>(kSize * kSize);
    scene.rivers.size = kSize;
    scene.rivers.metersPerPixel = 100.0;
    scene.rivers.receiver.assign(cells, terrain::kNoReceiver);
    scene.rivers.catchment.assign(cells, 1.0e4);
    scene.rivers.river.assign(cells, 0);
    for (int row = 0; row < kSize; ++row) {
        for (int column = 0; column < kSize; ++column) {
            const float height = column == 0 ? -100.0f : 50.0f * static_cast<float>(column);
            scene.grid.heights.push_back(height);
        }
    }
    for (int column = 1; column < kSize; ++column) {
        const std::size_t i = scene.grid.index(column, 4);
        scene.rivers.river[i] = 1;
        scene.rivers.catchment[i] = 1.0e6 * (kSize - column);
        scene.rivers.receiver[i] = static_cast<int>(scene.grid.index(column - 1, 4));
    }
    scene.rivers.cascades.push_back(Cascade{.column = 5, .row = 4});
    return scene;
}

void testRiverMapColors() {
    const Scene scene = makeScene();
    const Image image = terrain::renderRiverMap(scene.grid, scene.rivers);
    TEST_CHECK(image.width == 8 && image.height == 8 && image.channels == 3);
    TEST_CHECK(image.pixels.size() == 8 * 8 * 3);

    bool colors = true;
    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            const Rgb color = pixel(image, column, row);
            const bool nearCascade = std::abs(column - 5) <= 1 && std::abs(row - 4) <= 1;
            if (nearCascade) {
                colors = colors && hasColor(color, terrain::kCascadeRgb);
            } else if (column == 0) {
                colors = colors && hasColor(color, terrain::kOceanRgb);
            } else if (row == 4) {
                colors = colors && color.b > color.r && color.b > color.g; // river: blue
            } else {
                colors = colors && color.r == color.g && color.g == color.b; // land: gray
            }
        }
    }
    TEST_CHECK(colors);
    // Larger rivers are drawn in a deeper blue.
    TEST_CHECK(pixel(image, 1, 4).r < pixel(image, 7, 4).r);
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void testExportWritesThreeImages() {
    const terrain::HeightmapRegion region{.metersPerPixel = 200.0, .size = 48};
    const terrain::ErodedRegion eroded(terrain::kDefaultWorldSeed, region);
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "terrain_eroded_png_test";
    std::filesystem::remove_all(directory);
    TEST_CHECK(terrain::exportErodedPngs(eroded, directory));

    const HeightGrid& grid = eroded.grid();
    TEST_CHECK(readFile(directory / "heightmap_eroded.png") ==
               terrain::encodePng(terrain::renderHeightmapGray(grid)));
    TEST_CHECK(readFile(directory / "heightmap_eroded_preview.png") ==
               terrain::encodePng(terrain::renderHeightmapPreview(grid)));
    TEST_CHECK(readFile(directory / "rivers.png") ==
               terrain::encodePng(terrain::renderRiverMap(grid, eroded.rivers())));
    std::filesystem::remove_all(directory);
}

} // namespace

int main() {
    testRiverMapColors();
    testExportWritesThreeImages();
    return test::result();
}

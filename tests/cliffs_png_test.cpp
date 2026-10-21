#include "terrain/generation/cliff_region.hpp"
#include "terrain/io/cliffs_png.hpp"
#include "terrain/io/eroded_png.hpp"
#include "terrain/io/png.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

using terrain::Cirque;
using terrain::CliffRegion;
using terrain::HeightGrid;
using terrain::Image;

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

// Small regions keep the tests fast (the build is unoptimized). The cliff site of the world
// has strata walls, but no ocean and only small rivers.
const CliffRegion& cliffSite() {
    static const CliffRegion region(
        terrain::kDefaultWorldSeed,
        terrain::findCliffRegion(terrain::kDefaultWorldSeed, 50.0, 65));
    return region;
}

// A coastal region with oceans, rivers and a cascade, so the overlay checks are not empty
// (found by a search of the default world).
const CliffRegion& coast() {
    static const CliffRegion region(
        terrain::kDefaultWorldSeed,
        terrain::HeightmapRegion{
            .centerX = -66800.0, .centerZ = -2800.0, .metersPerPixel = 100.0, .size = 128});
    return region;
}

// Distance in pixels from the sample (column, row) to the axis of a cirque.
double pixelsToAxis(const CliffRegion& region, const Cirque& cirque, int column, int row) {
    const terrain::ErodedRegion& eroded = region.eroded();
    return std::hypot(eroded.sampleX(column) - cirque.x, eroded.sampleZ(row) - cirque.z) /
           eroded.region().metersPerPixel;
}

// Radius in pixels of the ring drawn around a cirque axis.
double ringRadius(const CliffRegion& region) {
    const double radius = static_cast<double>(region.field().params().cirqueRadius);
    return std::max(2.0, radius / region.eroded().region().metersPerPixel);
}

// True if a cirque ring or a cascade dot may have been drawn over the sample.
bool maybeOverdrawn(const CliffRegion& region, int column, int row) {
    const double radius = ringRadius(region);
    for (const Cirque& cirque : region.cirques()) {
        if (std::abs(pixelsToAxis(region, cirque, column, row) - radius) < 1.5) {
            return true;
        }
    }
    for (const terrain::Cascade& cascade : region.eroded().rivers().cascades) {
        if (std::abs(column - cascade.column) <= 1 && std::abs(row - cascade.row) <= 1) {
            return true;
        }
    }
    return false;
}

// Largest drop from the surface at (column, row) to one of its 4-neighbors.
float largestDrop(const HeightGrid& surface, int column, int row) {
    const int offsets[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    float drop = 0.0f;
    for (const auto& offset : offsets) {
        const int c = column + offset[0];
        const int r = row + offset[1];
        if (c >= 0 && r >= 0 && c < surface.size && r < surface.size) {
            drop = std::max(drop, surface.at(column, row) - surface.at(c, r));
        }
    }
    return drop;
}

// The coastal region has oceans, river cells and a cascade (it may also have cirques, which
// depend on the cliff parameters, so they are only checked where they exist).
void testCoastHasOverlays() {
    const CliffRegion& region = coast();
    const auto& heights = region.eroded().grid().heights;
    const auto& river = region.eroded().rivers().river;
    TEST_CHECK(std::any_of(heights.begin(), heights.end(),
                           [](float height) { return height < terrain::kSeaLevel; }));
    TEST_CHECK(std::any_of(river.begin(), river.end(),
                           [](std::uint8_t cell) { return cell != 0; }));
    TEST_CHECK(!region.eroded().rivers().cascades.empty());
}

void testMaskImage(const CliffRegion& region) {
    const int size = region.eroded().region().size;
    const Image mask = terrain::renderCliffMask(region);
    TEST_CHECK(mask.width == size && mask.height == size && mask.channels == 1);
    TEST_CHECK(mask.pixels.size() == region.mask().size());

    bool levels = mask.pixels.size() == region.mask().size();
    for (std::size_t i = 0; levels && i < region.mask().size(); ++i) {
        const float clamped = std::clamp(region.mask()[i], 0.0f, 1.0f);
        levels = mask.pixels[i] == std::lround(255.0f * clamped);
    }
    TEST_CHECK(levels);
}

void testPreviewSize(const CliffRegion& region) {
    const int size = region.eroded().region().size;
    const Image preview = terrain::renderCliffPreview(region);
    TEST_CHECK(preview.width == size && preview.height == size && preview.channels == 3);
    TEST_CHECK(preview.pixels.size() == static_cast<std::size_t>(size * size * 3));
}

void testOceanRiverAndCascadeColors(const CliffRegion& region) {
    const Image preview = terrain::renderCliffPreview(region);
    const HeightGrid& grid = region.eroded().grid();
    const terrain::RiverNetwork& rivers = region.eroded().rivers();

    bool oceans = true;
    bool blueRivers = true;
    for (int row = 0; row < grid.size; ++row) {
        for (int column = 0; column < grid.size; ++column) {
            if (maybeOverdrawn(region, column, row)) {
                continue;
            }
            const Rgb color = pixel(preview, column, row);
            if (grid.at(column, row) < terrain::kSeaLevel) {
                oceans = oceans && hasColor(color, terrain::kOceanRgb);
            } else if (rivers.isRiver(row * grid.size + column)) {
                blueRivers = blueRivers && color.b > color.r && color.b > color.g;
            }
        }
    }
    TEST_CHECK(oceans);
    TEST_CHECK(blueRivers);

    bool cascades = true;
    for (const terrain::Cascade& cascade : rivers.cascades) {
        cascades = cascades &&
                   hasColor(pixel(preview, cascade.column, cascade.row), terrain::kCascadeRgb);
    }
    TEST_CHECK(cascades);
}

// Walls and gorge floors are only drawn where their conditions hold (other overlays may hide
// some of the samples that qualify).
void testWallsAndGorgeFloors(const CliffRegion& region) {
    const Image preview = terrain::renderCliffPreview(region);
    const HeightGrid& surface = region.surface();
    const float wallDrop = 0.5f * region.field().params().stratumHeight;

    bool walls = true;
    bool gorges = true;
    for (int row = 0; row < surface.size; ++row) {
        for (int column = 0; column < surface.size; ++column) {
            const Rgb color = pixel(preview, column, row);
            const std::size_t i = surface.index(column, row);
            if (hasColor(color, terrain::kCliffWallRgb)) {
                walls = walls && surface.at(column, row) >= terrain::kSeaLevel &&
                        largestDrop(surface, column, row) >= wallDrop;
            }
            if (hasColor(color, terrain::kGorgeRgb)) {
                gorges = gorges && region.gorgeHalfWidth()[i] > 0.0f &&
                         region.riverDistance()[i] < region.gorgeHalfWidth()[i] &&
                         region.mask()[i] > 0.0f &&
                         region.eroded().grid().heights[i] - surface.heights[i] >= 2.0f;
            }
        }
    }
    TEST_CHECK(walls);
    TEST_CHECK(gorges);
}

// Every ring pixel lies on the circle around a cirque axis, and each cirque leaves some.
void testCirqueRings(const CliffRegion& region) {
    const Image preview = terrain::renderCliffPreview(region);
    const double radius = ringRadius(region);

    int ringPixels = 0;
    bool onRings = true;
    for (int row = 0; row < preview.height; ++row) {
        for (int column = 0; column < preview.width; ++column) {
            if (!hasColor(pixel(preview, column, row), terrain::kCirqueRgb)) {
                continue;
            }
            ++ringPixels;
            bool onRing = false;
            for (const Cirque& cirque : region.cirques()) {
                const double distance = pixelsToAxis(region, cirque, column, row);
                onRing = onRing || std::abs(distance - radius) <= 0.5 + 1.0e-6;
            }
            onRings = onRings && onRing;
        }
    }
    TEST_CHECK(onRings);
    TEST_CHECK(region.cirques().empty() ? ringPixels == 0 : ringPixels > 0);
}

void testDeterminism(const CliffRegion& region) {
    TEST_CHECK(terrain::renderCliffMask(region).pixels == terrain::renderCliffMask(region).pixels);
    TEST_CHECK(terrain::renderCliffPreview(region).pixels ==
               terrain::renderCliffPreview(region).pixels);
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

bool hasPngSignature(const std::vector<std::uint8_t>& bytes) {
    constexpr std::uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    return bytes.size() > 8 && std::equal(kSignature, kSignature + 8, bytes.begin());
}

void testExportWritesTwoImages() {
    const CliffRegion& region = cliffSite();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "terrain_cliffs_png_test";
    std::filesystem::remove_all(directory);
    TEST_CHECK(terrain::exportCliffPngs(region, directory));

    const std::vector<std::uint8_t> mask = readFile(directory / "cliffs_mask.png");
    const std::vector<std::uint8_t> preview = readFile(directory / "cliffs_preview.png");
    TEST_CHECK(hasPngSignature(mask));
    TEST_CHECK(hasPngSignature(preview));
    TEST_CHECK(mask == terrain::encodePng(terrain::renderCliffMask(region)));
    TEST_CHECK(preview == terrain::encodePng(terrain::renderCliffPreview(region)));
    std::filesystem::remove_all(directory);
}

void testExportFailsOnRegularFile() {
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "terrain_cliffs_png_test_file";
    std::filesystem::remove_all(file);
    std::ofstream(file) << "not a directory";
    TEST_CHECK(std::filesystem::is_regular_file(file));
    TEST_CHECK(!terrain::exportCliffPngs(cliffSite(), file));
    std::filesystem::remove_all(file);
}

} // namespace

int main() {
    testCoastHasOverlays();
    for (const CliffRegion* region : {&cliffSite(), &coast()}) {
        testMaskImage(*region);
        testPreviewSize(*region);
        testOceanRiverAndCascadeColors(*region);
        testWallsAndGorgeFloors(*region);
        testCirqueRings(*region);
        testDeterminism(*region);
    }
    testExportWritesTwoImages();
    testExportFailsOnRegularFile();
    return test::result();
}

#include "terrain/generation/sculpted_region.hpp"
#include "terrain/io/arches_png.hpp"
#include "terrain/io/cliffs_png.hpp"
#include "terrain/io/png.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

using terrain::Arch;
using terrain::Image;
using terrain::SculptedRegion;

struct Rgb {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;

    friend bool operator==(const Rgb&, const Rgb&) = default;
};

Rgb pixel(const Image& image, int column, int row) {
    const auto offset = static_cast<std::size_t>((row * image.width + column) * 3);
    return {image.pixels[offset], image.pixels[offset + 1], image.pixels[offset + 2]};
}

bool hasColor(Rgb color, const std::uint8_t (&rgb)[3]) {
    return color.r == rgb[0] && color.g == rgb[1] && color.b == rgb[2];
}

// Small regions keep the tests fast (the build is unoptimized): the cliff site of the world,
// with walls, overhangs and arches, and a coastal region with oceans, rivers and arches.
const SculptedRegion& cliffSite() {
    static const SculptedRegion region(
        terrain::kDefaultWorldSeed,
        terrain::findCliffRegion(terrain::kDefaultWorldSeed, 50.0, 65));
    return region;
}

const SculptedRegion& coast() {
    static const SculptedRegion region(
        terrain::kDefaultWorldSeed,
        terrain::HeightmapRegion{.centerX = 10000.0, .centerZ = 10000.0,
                                 .metersPerPixel = 100.0, .size = 128});
    return region;
}

// Distance in pixels from the sample (column, row) to the center of an arch.
double pixelsToArch(const SculptedRegion& region, const Arch& arch, int column, int row) {
    const terrain::ErodedRegion& eroded = region.cliffs().eroded();
    return std::hypot(eroded.sampleX(column) - arch.x, eroded.sampleZ(row) - arch.z) /
           eroded.region().metersPerPixel;
}

// Half-length in pixels of the segment drawn for an arch.
double halfLength(const SculptedRegion& region, const Arch& arch) {
    return std::max(terrain::kMinArchHalfLength,
                    arch.halfSpan / region.cliffs().eroded().region().metersPerPixel);
}

// True if an arch symbol may cover the sample.
bool nearArch(const SculptedRegion& region, int column, int row) {
    return std::any_of(region.arches().begin(), region.arches().end(), [&](const Arch& arch) {
        return pixelsToArch(region, arch, column, row) <= halfLength(region, arch) + 1.0;
    });
}

// ---------------------------------------------------------------------------

void testHasContent() {
    std::printf("cliff site: %zu arches; coast: %zu arches\n", cliffSite().arches().size(),
                coast().arches().size());
    TEST_CHECK(!cliffSite().arches().empty());
    TEST_CHECK(!coast().arches().empty());
    const auto& air = cliffSite().airBelowRock();
    TEST_CHECK(std::count(air.begin(), air.end(), std::uint8_t{1}) > 0);
}

void testMaskImage(const SculptedRegion& region) {
    const int size = region.surface().size;
    const Image mask = terrain::renderOverhangMask(region);
    TEST_CHECK(mask.width == size && mask.height == size && mask.channels == 1);
    TEST_CHECK(mask.pixels.size() == region.controlMask().size());
    bool levels = mask.pixels.size() == region.controlMask().size();
    for (std::size_t i = 0; levels && i < region.controlMask().size(); ++i) {
        const float clamped = std::clamp(region.controlMask()[i], 0.0f, 1.0f);
        levels = mask.pixels[i] == std::lround(255.0f * clamped);
    }
    TEST_CHECK(levels);
}

// The preview is the cliff preview, changed only by the overhang samples (orange) and the
// arch symbols; rivers, oceans and cascades are never drawn over.
void testPreviewOverlays(const SculptedRegion& region) {
    const Image cliffs = terrain::renderCliffPreview(region.cliffs());
    const Image preview = terrain::renderArchesPreview(region);
    TEST_CHECK(preview.width == cliffs.width && preview.height == cliffs.height &&
               preview.channels == 3);
    const terrain::HeightGrid& grid = region.cliffs().eroded().grid();
    const terrain::RiverNetwork& rivers = region.cliffs().eroded().rivers();
    bool explained = true;
    bool overhangs = true;
    bool water = true;
    for (int row = 0; row < grid.size; ++row) {
        for (int column = 0; column < grid.size; ++column) {
            const Rgb color = pixel(preview, column, row);
            const bool air = region.airBelowRock()[grid.index(column, row)] != 0;
            const bool arch = nearArch(region, column, row);
            if (!(color == pixel(cliffs, column, row))) {
                explained = explained && ((air && hasColor(color, terrain::kOverhangRgb)) ||
                                          (arch && (hasColor(color, terrain::kArchSpanRgb) ||
                                                    hasColor(color, terrain::kArchPillarRgb))));
            }
            overhangs = overhangs && (!air || arch || hasColor(color, terrain::kOverhangRgb));
            if (grid.at(column, row) < terrain::kSeaLevel || rivers.isRiver(row * grid.size + column)) {
                water = water && color == pixel(cliffs, column, row);
            }
        }
    }
    bool cascades = true;
    for (const terrain::Cascade& cascade : rivers.cascades) {
        for (int row = cascade.row - 1; row <= cascade.row + 1; ++row) {
            for (int column = cascade.column - 1; column <= cascade.column + 1; ++column) {
                if (column >= 0 && row >= 0 && column < grid.size && row < grid.size) {
                    cascades = cascades && pixel(preview, column, row) == pixel(cliffs, column, row);
                }
            }
        }
    }
    TEST_CHECK(explained);
    TEST_CHECK(overhangs);
    TEST_CHECK(water);
    TEST_CHECK(cascades);
}

// Each arch leaves a red span near its center and two white pillars at its ends.
void testArchSymbols(const SculptedRegion& region) {
    const Image preview = terrain::renderArchesPreview(region);
    const terrain::HeightmapRegion& area = region.cliffs().eroded().region();
    bool drawn = true;
    for (const Arch& arch : region.arches()) {
        const double column = (arch.x - area.centerX) / area.metersPerPixel + area.size / 2.0 - 0.5;
        const double row = (arch.z - area.centerZ) / area.metersPerPixel + area.size / 2.0 - 0.5;
        const double half = halfLength(region, arch);
        for (const double side : {-1.0, 1.0}) {
            const auto c = static_cast<int>(std::lround(column + side * half * arch.directionX));
            const auto r = static_cast<int>(std::lround(row + side * half * arch.directionZ));
            const bool inside = c >= 0 && r >= 0 && c < area.size && r < area.size;
            drawn = drawn && (!inside || hasColor(pixel(preview, c, r), terrain::kArchPillarRgb));
        }
        bool span = false;
        for (int r = static_cast<int>(row) - 1; r <= static_cast<int>(row) + 2; ++r) {
            for (int c = static_cast<int>(column) - 1; c <= static_cast<int>(column) + 2; ++c) {
                span = span || (c >= 0 && r >= 0 && c < area.size && r < area.size &&
                                hasColor(pixel(preview, c, r), terrain::kArchSpanRgb));
            }
        }
        drawn = drawn && span;
    }
    TEST_CHECK(drawn);
}

void testDeterminism(const SculptedRegion& region) {
    TEST_CHECK(terrain::renderOverhangMask(region).pixels ==
               terrain::renderOverhangMask(region).pixels);
    TEST_CHECK(terrain::renderArchesPreview(region).pixels ==
               terrain::renderArchesPreview(region).pixels);
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
    const SculptedRegion& region = cliffSite();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "terrain_arches_png_test";
    std::filesystem::remove_all(directory);
    TEST_CHECK(terrain::exportArchPngs(region, directory));

    const std::vector<std::uint8_t> mask = readFile(directory / "overhangs_mask.png");
    const std::vector<std::uint8_t> preview = readFile(directory / "arches_preview.png");
    TEST_CHECK(hasPngSignature(mask));
    TEST_CHECK(hasPngSignature(preview));
    TEST_CHECK(mask == terrain::encodePng(terrain::renderOverhangMask(region)));
    TEST_CHECK(preview == terrain::encodePng(terrain::renderArchesPreview(region)));
    std::filesystem::remove_all(directory);
}

void testExportFailsOnRegularFile() {
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "terrain_arches_png_test_file";
    std::filesystem::remove_all(file);
    std::ofstream(file) << "not a directory";
    TEST_CHECK(std::filesystem::is_regular_file(file));
    TEST_CHECK(!terrain::exportArchPngs(cliffSite(), file));
    std::filesystem::remove_all(file);
}

} // namespace

int main() {
    testHasContent();
    for (const SculptedRegion* region : {&cliffSite(), &coast()}) {
        testMaskImage(*region);
        testPreviewOverlays(*region);
        testArchSymbols(*region);
        testDeterminism(*region);
    }
    testExportWritesTwoImages();
    testExportFailsOnRegularFile();
    return test::result();
}

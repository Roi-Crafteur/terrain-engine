// Cave images: the vertical section and the map of the caves under a small eroded region with
// a river and surface entrances, an ocean without any entrance, and the files of the export.
#include "terrain/generation/cave_region.hpp"
#include "terrain/io/caves_png.hpp"
#include "terrain/io/heightmap_png.hpp"
#include "terrain/io/png.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace {

using terrain::CaveEntrance;
using terrain::CavePreviewStats;
using terrain::CaveRegion;
using terrain::CaveSlice;
using terrain::Image;

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

Rgb rgb(const std::uint8_t (&c)[3]) {
    return {c[0], c[1], c[2]};
}

bool hasColor(Rgb color, const std::uint8_t (&c)[3]) {
    return color == rgb(c);
}

template <class Predicate>
int countPixels(const Image& image, Predicate predicate) {
    int count = 0;
    for (int row = 0; row < image.height; ++row) {
        for (int column = 0; column < image.width; ++column) {
            count += predicate(pixel(image, column, row)) ? 1 : 0;
        }
    }
    return count;
}

int countColor(const Image& image, const std::uint8_t (&c)[3]) {
    return countPixels(image, [&](Rgb color) { return hasColor(color, c); });
}

bool isAir(Rgb color) {
    return hasColor(color, terrain::kSpaghettiAirRgb) || hasColor(color, terrain::kNoodleAirRgb) ||
           hasColor(color, terrain::kCheeseAirRgb) || hasColor(color, terrain::kRavineAirRgb) ||
           hasColor(color, terrain::kEntranceAirRgb);
}

// Rock color at some depth: the color at the ground, scaled.
bool isRock(Rgb color) {
    const double light = color.r / static_cast<double>(terrain::kCaveRockRgb[0]);
    return std::abs(color.g - terrain::kCaveRockRgb[1] * light) <= 1.5 &&
           std::abs(color.b - terrain::kCaveRockRgb[2] * light) <= 1.5;
}

// Land with a river and entrances: 64 px at 32 m/px (2 km). Small, as the build is unoptimized.
const CaveRegion& riverSite() {
    static const CaveRegion region(
        terrain::kDefaultWorldSeed,
        terrain::HeightmapRegion{.centerX = 10000.0, .centerZ = -20000.0,
                                 .metersPerPixel = 32.0, .size = 64});
    return region;
}

// Open sea, 32 px at 100 m/px: no entrance, and the sea over the ground.
const CaveRegion& openSea() {
    static const CaveRegion region(
        terrain::kDefaultWorldSeed,
        terrain::HeightmapRegion{.centerX = -10000.0, .centerZ = 0.0,
                                 .metersPerPixel = 100.0, .size = 32});
    return region;
}

struct Maps {
    CaveSlice slice;
    Image sliceImage;
    Image preview;
    CavePreviewStats stats;
};

const Maps& riverMaps() {
    static const Maps maps = [] {
        Maps result;
        result.slice = terrain::defaultCaveSlice(riverSite());
        result.sliceImage = terrain::renderCaveSlice(riverSite(), result.slice);
        result.preview = terrain::renderCavePreview(riverSite(), &result.stats);
        return result;
    }();
    return maps;
}

std::vector<CaveEntrance> entrancesOf(const CaveRegion& region) {
    const terrain::HeightmapRegion& area = region.eroded().region();
    const double half = area.size * area.metersPerPixel / 2.0;
    return region.field().entrancesNear(area.centerX - half, area.centerZ - half,
                                        area.centerX + half, area.centerZ + half,
                                        region.ground());
}

bool siteInside(const CaveRegion& region, const CaveEntrance& entrance) {
    const terrain::HeightmapRegion& area = region.eroded().region();
    const double half = area.size * area.metersPerPixel / 2.0;
    return std::abs(entrance.topX - area.centerX) <= half &&
           std::abs(entrance.topZ - area.centerZ) <= half;
}

struct Point {
    double x;
    double z;
};

// World point of a pixel column of a section.
Point pointOf(const CaveSlice& slice, int column) {
    const double center = slice.alongX ? slice.centerX : slice.centerZ;
    const double u = center + (column + 0.5 - slice.width / 2.0) * slice.metersPerPixel;
    return slice.alongX ? Point{u, slice.centerZ} : Point{slice.centerX, u};
}

double groundAlong(const CaveRegion& region, const CaveSlice& slice, int column) {
    const Point point = pointOf(slice, column);
    return region.groundAt(point.x, point.z);
}

double rowHeight(const CaveSlice& slice, int row) {
    return slice.top - (row + 0.5) * slice.metersPerPixel;
}

// ---------------------------------------------------------------------------

void testPalettesDistinct() {
    const Rgb colors[] = {
        rgb(terrain::kCaveSkyRgb), rgb(terrain::kCaveSeaRgb), rgb(terrain::kCaveRockRgb),
        rgb(terrain::kCaveGroundRgb), rgb(terrain::kCaveRiverRgb), rgb(terrain::kSpaghettiAirRgb),
        rgb(terrain::kNoodleAirRgb), rgb(terrain::kCheeseAirRgb), rgb(terrain::kRavineAirRgb),
        rgb(terrain::kEntranceAirRgb), rgb(terrain::kCaveShallowRgb), rgb(terrain::kCaveDeepRgb),
        rgb(terrain::kCaveEntranceMarkRgb), rgb(terrain::kCaveSliceLineRgb)};
    for (std::size_t a = 0; a < std::size(colors); ++a) {
        for (std::size_t b = 0; b < a; ++b) {
            TEST_CHECK(!(colors[a] == colors[b]));
        }
    }
}

// The section goes through the middle of the axis of the entrance of the region nearest its
// center among those whose tube follows an axis within 0.4 (rise over run), along that axis.
void testDefaultSliceThroughEntrance() {
    const CaveRegion& region = riverSite();
    const CaveSlice& slice = riverMaps().slice;
    const terrain::HeightmapRegion& area = region.eroded().region();
    TEST_CHECK(slice.width == 1024 && slice.height >= 1 && slice.height <= 640);
    double nearest = std::numeric_limits<double>::max();
    double chosen = -1.0;
    for (const CaveEntrance& e : entrancesOf(region)) {
        const double dx = e.bottomX - e.topX;
        const double dz = e.bottomZ - e.topZ;
        const double distance = std::hypot(e.topX - area.centerX, e.topZ - area.centerZ);
        if (siteInside(region, e) &&
            std::min(std::abs(dx), std::abs(dz)) <= 0.4 * std::max(std::abs(dx), std::abs(dz))) {
            nearest = std::min(nearest, distance);
            if ((e.topX + e.bottomX) / 2.0 == slice.centerX &&
                (e.topZ + e.bottomZ) / 2.0 == slice.centerZ &&
                slice.alongX == (std::abs(dx) >= std::abs(dz))) {
                chosen = distance;
            }
        }
    }
    TEST_CHECK(chosen >= 0.0 && chosen == nearest);
}

void testDefaultSliceFitsTheGround() {
    const CaveRegion& region = riverSite();
    const CaveSlice& slice = riverMaps().slice;
    double lowest = std::numeric_limits<double>::max();
    double highest = std::numeric_limits<double>::lowest();
    for (int column = 0; column < slice.width; ++column) {
        const double ground = groundAlong(region, slice, column);
        lowest = std::min(lowest, ground);
        highest = std::max(highest, ground);
    }
    TEST_CHECK(std::abs(slice.top - (highest + 16.0)) < 1e-6);
    const double wanted = lowest - region.field().params().maxDepth - 16.0;
    const double bottom = slice.top - slice.height * slice.metersPerPixel;
    TEST_CHECK(slice.height == 640 ||
               (bottom <= wanted + 1e-6 && bottom + slice.metersPerPixel > wanted));
}

void testSliceContent() {
    const Maps& maps = riverMaps();
    const Image& image = maps.sliceImage;
    TEST_CHECK(image.width == maps.slice.width && image.height == maps.slice.height);
    TEST_CHECK(image.channels == 3 && maps.slice.metersPerPixel == 0.5);
    TEST_CHECK(image.pixels.size() == static_cast<std::size_t>(image.width * image.height * 3));
    TEST_CHECK(terrain::renderCaveSlice(riverSite(), maps.slice).pixels == image.pixels);
    const int sky = countColor(image, terrain::kCaveSkyRgb);
    const int rock = countPixels(image, isRock);
    const int air = countPixels(image, isAir);
    const int entrance = countColor(image, terrain::kEntranceAirRgb);
    const int line = countColor(image, terrain::kCaveGroundRgb);
    TEST_CHECK(sky > 0 && rock > 0 && air > 0 && entrance > 0);
    TEST_CHECK(line >= image.width / 2);
}

// Sky over the ground, or the sea under kSeaLevel, then the line of the ground (blue in the
// columns of rivers) unless carved, then rock, darker with the depth (red never rises).
void testSliceLayers(const CaveRegion& region, const CaveSlice& slice, const Image& image) {
    const auto riverReach = static_cast<float>(region.eroded().region().metersPerPixel) / 2.0f;
    bool above = true;
    bool line = true;
    bool rock = true;
    for (int column = 0; column < slice.width; ++column) {
        const double ground = groundAlong(region, slice, column);
        const Point point = pointOf(slice, column);
        const bool river = region.riverDistanceAt(point.x, point.z) < riverReach;
        int previousRed = 256;
        for (int row = 0; row < slice.height; ++row) {
            const double y = rowHeight(slice, row);
            const Rgb color = pixel(image, column, row);
            if (y >= ground) {
                above = above && (y < terrain::kSeaLevel ? hasColor(color, terrain::kCaveSeaRgb)
                                                         : hasColor(color, terrain::kCaveSkyRgb));
            } else if (ground - y <= slice.metersPerPixel) {
                line = line && (isAir(color) || hasColor(color, river ? terrain::kCaveRiverRgb
                                                                      : terrain::kCaveGroundRgb));
            } else if (!isAir(color)) {
                rock = rock && isRock(color) && color.r <= previousRed;
                previousRed = color.r;
            }
        }
    }
    TEST_CHECK(above && line && rock);
}

// Component of the cave air at a point: the one with the smallest void within its limits.
Rgb expectedAirColor(const terrain::CaveVoids& voids) {
    const std::pair<float, Rgb> candidates[] = {
        {std::max(voids.entrances, voids.entranceLimit), rgb(terrain::kEntranceAirRgb)},
        {std::max(voids.spaghetti, voids.limit), rgb(terrain::kSpaghettiAirRgb)},
        {std::max(voids.noodles, voids.limit), rgb(terrain::kNoodleAirRgb)},
        {std::max(voids.cheese, voids.limit), rgb(terrain::kCheeseAirRgb)},
        {std::max(voids.ravines, voids.limit), rgb(terrain::kRavineAirRgb)},
    };
    return std::min_element(std::begin(candidates), std::end(candidates),
                            [](const auto& a, const auto& b) { return a.first < b.first; })
        ->second;
}

// Cave air is exactly the carved density (>= 0 under the ground), colored by its component.
void testSliceAgainstDensity(const CaveRegion& region, const CaveSlice& slice,
                             const Image& image) {
    bool carved = true;
    bool components = true;
    int airSeen = 0;
    for (int column = 0; column < slice.width; column += 7) {
        const double ground = groundAlong(region, slice, column);
        const Point point = pointOf(slice, column);
        for (int row = 0; row < slice.height; ++row) {
            const double y = rowHeight(slice, row);
            const Rgb color = pixel(image, column, row);
            if (y >= ground || (row % 5 != 0 && !isAir(color))) {
                continue;
            }
            const bool air = region.densityAt(point.x, y, point.z) >= 0.0f;
            carved = carved && air == isAir(color);
            if (air && ++airSeen % 40 == 0) {
                terrain::CaveSampler sampler =
                    region.sampler({point.x, y, point.z, point.x, y, point.z});
                const terrain::CaveColumn caves =
                    sampler.column(point.x, point.z, static_cast<float>(ground));
                components = components && color == expectedAirColor(sampler.voids(caves, y));
            }
        }
    }
    TEST_CHECK(airSeen > 0);
    TEST_CHECK(carved);
    TEST_CHECK(components);
}

void testSliceAlongZ() {
    const CaveRegion& region = riverSite();
    CaveSlice slice = riverMaps().slice;
    slice.alongX = false;
    const Image image = terrain::renderCaveSlice(region, slice);
    TEST_CHECK(image.width == slice.width && image.height == slice.height);
    testSliceLayers(region, slice, image);
    testSliceAgainstDensity(region, slice, image);
}

// Open sea: no entrance, so the section runs through the center of the region along x, with
// the sea over the ground; and a map without entrance symbols.
void testOpenSea() {
    const CaveRegion& region = openSea();
    const terrain::HeightmapRegion& area = region.eroded().region();
    TEST_CHECK(entrancesOf(region).empty());
    const CaveSlice slice = terrain::defaultCaveSlice(region);
    TEST_CHECK(slice.centerX == area.centerX && slice.centerZ == area.centerZ && slice.alongX);
    TEST_CHECK(slice.height >= 1 && slice.height <= 640);
    const Image image = terrain::renderCaveSlice(region, slice);
    TEST_CHECK(countColor(image, terrain::kCaveSeaRgb) > 0);
    testSliceLayers(region, slice, image);

    CavePreviewStats stats;
    const Image preview = terrain::renderCavePreview(region, &stats);
    TEST_CHECK(preview.width == 32 && preview.height == 32 && preview.channels == 3);
    TEST_CHECK(stats.entrances == 0 && countColor(preview, terrain::kCaveEntranceMarkRgb) == 0);
    TEST_CHECK(terrain::renderCavePreview(region).pixels == preview.pixels);
}

// Color of the pixel of the map nearest to a point of the world, if it lies on the map.
std::optional<Rgb> colorNear(const CaveRegion& region, const Image& map, double x, double z) {
    const terrain::ErodedRegion& eroded = region.eroded();
    const double step = eroded.region().metersPerPixel;
    const auto column = static_cast<int>(std::lround((x - eroded.sampleX(0)) / step));
    const auto row = static_cast<int>(std::lround((z - eroded.sampleZ(0)) / step));
    if (column < 0 || row < 0 || column >= map.width || row >= map.height) {
        return std::nullopt;
    }
    return pixel(map, column, row);
}

void testPreviewOverlays() {
    const CaveRegion& region = riverSite();
    const Image& preview = riverMaps().preview;
    const CavePreviewStats& stats = riverMaps().stats;
    const terrain::ErodedRegion& eroded = region.eroded();
    const int size = eroded.region().size;
    TEST_CHECK(preview.width == size && preview.height == size && preview.channels == 3);
    const Image base = terrain::renderHeightmapPreview(eroded.grid());
    const int marks = countColor(preview, terrain::kCaveEntranceMarkRgb);
    const int lines = countColor(preview, terrain::kCaveSliceLineRgb);
    int rivers = 0;
    int tinted = 0;
    bool riversKept = true;
    for (int row = 0; row < size; ++row) {
        for (int column = 0; column < size; ++column) {
            const Rgb color = pixel(preview, column, row);
            const bool overlay = hasColor(color, terrain::kCaveEntranceMarkRgb) ||
                                 hasColor(color, terrain::kCaveSliceLineRgb);
            if (eroded.rivers().isRiver(row * size + column)) {
                ++rivers;
                riversKept = riversKept && (hasColor(color, terrain::kCaveRiverRgb) || overlay);
            } else if (!overlay && !(color == pixel(base, column, row))) {
                ++tinted;
            }
        }
    }
    TEST_CHECK(rivers > 0 && countColor(preview, terrain::kCaveRiverRgb) > 0);
    TEST_CHECK(riversKept && marks > 0 && lines >= 8);

    // Tinted samples are the caved ones, but those under an entrance or the line.
    const auto caved = std::lround(stats.cavedFraction * size * size);
    TEST_CHECK(stats.cavedFraction > 0.0 && stats.cavedFraction < 1.0);
    TEST_CHECK(tinted <= caved && caved <= tinted + marks + lines);

    // Entrances of the region: counted when their site lies inside, drawn at their bottom.
    std::size_t inside = 0;
    bool drawn = true;
    for (const CaveEntrance& e : entrancesOf(region)) {
        inside += siteInside(region, e) ? 1 : 0;
        const std::optional<Rgb> color = colorNear(region, preview, e.bottomX, e.bottomZ);
        drawn = drawn && (!color || hasColor(*color, terrain::kCaveEntranceMarkRgb) ||
                          hasColor(*color, terrain::kCaveSliceLineRgb));
    }
    TEST_CHECK(inside > 0 && stats.entrances == inside && drawn);

    // The line of the section passes through its center.
    const CaveSlice& slice = riverMaps().slice;
    const std::optional<Rgb> center = colorNear(region, preview, slice.centerX, slice.centerZ);
    TEST_CHECK(center && hasColor(*center, terrain::kCaveSliceLineRgb));
}

// Depth of the shallowest cave under a sample, scanned every 4 m from just under the minimum
// roof to just under the maximum depth, or -1.
double shallowestDepth(const CaveRegion& region, double x, double z) {
    const terrain::CaveParams& params = region.field().params();
    const double ground = region.groundAt(x, z);
    for (double depth = params.minRoof - 1.0; depth <= params.maxDepth + 1.0; depth += 4.0) {
        if (region.densityAt(x, ground - depth, z) >= 0.0f) {
            return depth;
        }
    }
    return -1.0;
}

// A sample over a cave is tinted by the depth of its shallowest cave, from the shallow tint to
// the deep one; the others keep the colors of the relief.
void testPreviewTint() {
    const CaveRegion& region = riverSite();
    const Image& preview = riverMaps().preview;
    const terrain::ErodedRegion& eroded = region.eroded();
    const Image base = terrain::renderHeightmapPreview(eroded.grid());
    const terrain::CaveParams& params = region.field().params();
    const double first = params.minRoof - 1.0;
    const double span = params.maxDepth + 1.0 - first;
    int withCave = 0;
    bool tints = true;
    bool bare = true;
    for (int i = 0; i < preview.width * preview.height; i += 7) {
        const int column = i % preview.width;
        const int row = i / preview.width;
        const Rgb color = pixel(preview, column, row);
        if (eroded.rivers().isRiver(i) || hasColor(color, terrain::kCaveEntranceMarkRgb) ||
            hasColor(color, terrain::kCaveSliceLineRgb)) {
            continue;
        }
        const double depth = shallowestDepth(region, eroded.sampleX(column), eroded.sampleZ(row));
        if (depth < 0.0) {
            bare = bare && color == pixel(base, column, row);
            continue;
        }
        ++withCave;
        // Red channel of the tint, from the color over the relief (60% tint).
        const double tint = (color.r - 0.4 * pixel(base, column, row).r) / 0.6;
        const double observed =
            (terrain::kCaveShallowRgb[0] - tint) /
            (terrain::kCaveShallowRgb[0] - static_cast<double>(terrain::kCaveDeepRgb[0]));
        tints = tints && std::abs(observed - (depth - first) / span) < 0.06;
    }
    TEST_CHECK(withCave > 10 && tints && bare);
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

bool hasPngSignature(const std::vector<std::uint8_t>& bytes) {
    constexpr std::uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    return bytes.size() > 8 && std::equal(kSignature, kSignature + 8, bytes.begin());
}

// The files are the images rendered above: a second, independent render of both.
void testExportWritesTwoImages() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "terrain_caves_png_test";
    std::filesystem::remove_all(directory);
    CavePreviewStats stats;
    TEST_CHECK(terrain::exportCavePngs(riverSite(), directory, &stats));

    const std::vector<std::uint8_t> slice = readFile(directory / "cave_slice.png");
    const std::vector<std::uint8_t> preview = readFile(directory / "cave_preview.png");
    TEST_CHECK(hasPngSignature(slice) && hasPngSignature(preview));
    TEST_CHECK(slice == terrain::encodePng(riverMaps().sliceImage));
    TEST_CHECK(preview == terrain::encodePng(riverMaps().preview));
    TEST_CHECK(stats.entrances == riverMaps().stats.entrances &&
               stats.cavedFraction == riverMaps().stats.cavedFraction);
    std::filesystem::remove_all(directory);
}

void testExportFailsOnRegularFile() {
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "terrain_caves_png_test_file";
    std::filesystem::remove_all(file);
    std::ofstream(file) << "not a directory";
    TEST_CHECK(std::filesystem::is_regular_file(file));
    TEST_CHECK(!terrain::exportCavePngs(riverSite(), file));
    std::filesystem::remove_all(file);
}

} // namespace

int main() {
    testPalettesDistinct();
    testDefaultSliceThroughEntrance();
    testDefaultSliceFitsTheGround();
    testSliceContent();
    testSliceLayers(riverSite(), riverMaps().slice, riverMaps().sliceImage);
    testSliceAgainstDensity(riverSite(), riverMaps().slice, riverMaps().sliceImage);
    testSliceAlongZ();
    testOpenSea();
    testPreviewOverlays();
    testPreviewTint();
    testExportWritesTwoImages();
    testExportFailsOnRegularFile();
    return test::result();
}

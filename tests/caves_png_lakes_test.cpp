// Lakes, wall materials and speleothems of the cave images: the vertical section through a lake of
// a small region (and of the same region made freezing cold), through a speleothem along both
// axes, and the lakes of the map, all checked against an independent evaluation of the caves.
#include "terrain/generation/cave_region.hpp"
#include "terrain/io/caves_png.hpp"
#include "terrain/io/heightmap_png.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace {

using terrain::CaveColumn;
using terrain::CaveMaterial;
using terrain::CaveRegion;
using terrain::CaveSampler;
using terrain::CaveSlice;
using terrain::Image;
using terrain::Speleothem;
using terrain::SpeleothemKind;
using Speleothems = std::vector<Speleothem>;

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

Rgb materialColor(CaveMaterial material) {
    const std::array<std::uint8_t, 3> c = terrain::caveMaterialRgb(material);
    return {c[0], c[1], c[2]};
}

bool isDryAir(Rgb color) {
    return color == rgb(terrain::kSpaghettiAirRgb) || color == rgb(terrain::kNoodleAirRgb) ||
           color == rgb(terrain::kCheeseAirRgb) || color == rgb(terrain::kRavineAirRgb) ||
           color == rgb(terrain::kEntranceAirRgb);
}

bool isSpeleothem(Rgb color) {
    return color == rgb(terrain::kCaveSpeleothemRgb) ||
           color == rgb(terrain::kCaveSpeleothemIceRgb);
}

bool isFluid(Rgb color) {
    return color == rgb(terrain::kCaveWaterRgb) || color == rgb(terrain::kCaveIceRgb);
}

// Carved air: dry, under a lake, or with a speleothem drawn on it.
bool isCarved(Rgb color) {
    return isDryAir(color) || isFluid(color) || isSpeleothem(color);
}

// Rock color at some depth: the color at the ground, scaled.
bool isRock(Rgb color) {
    const double light = color.r / static_cast<double>(terrain::kCaveRockRgb[0]);
    return std::abs(color.g - terrain::kCaveRockRgb[1] * light) <= 1.5 &&
           std::abs(color.b - terrain::kCaveRockRgb[2] * light) <= 1.5;
}

bool isMaterial(Rgb color) {
    for (int material = 0; material < terrain::kCaveMaterialCount; ++material) {
        if (color == materialColor(static_cast<CaveMaterial>(material))) {
            return true;
        }
    }
    return false;
}

// A small land region, 1 km wide, with lakes in its caves and speleothems under its center; and
// the same region in a climate so cold that every lake is frozen (the relief does not change).
constexpr terrain::HeightmapRegion kArea{.centerX = 10000.0, .centerZ = -20000.0,
                                         .metersPerPixel = 32.0, .size = 32};

const CaveRegion& warmSite() {
    static const CaveRegion region(terrain::kDefaultWorldSeed, kArea);
    return region;
}

const CaveRegion& coldSite() {
    static const CaveRegion region(terrain::kDefaultWorldSeed, kArea, [] {
        terrain::CaveRegionParams params;
        params.ecology.climate.meanTemperature = -80.0f;
        return params;
    }());
    return region;
}

struct Point {
    double x;
    double z;
};

// World point of a pixel column of a section, height of a row.
Point pointOf(const CaveSlice& slice, int column) {
    const double center = slice.alongX ? slice.centerX : slice.centerZ;
    const double u = center + (column + 0.5 - slice.width / 2.0) * slice.metersPerPixel;
    return slice.alongX ? Point{u, slice.centerZ} : Point{slice.centerX, u};
}

double rowHeight(const CaveSlice& slice, int row) {
    return slice.top - (row + 0.5) * slice.metersPerPixel;
}

// Section of 129 columns along the given axis, the middle one through (x, z), with a row center
// at height y, from 24 m over the ground to 24 m under the deepest cave.
CaveSlice sliceThrough(const CaveRegion& region, double x, double y, double z, bool alongX) {
    CaveSlice slice;
    slice.centerX = x;
    slice.centerZ = z;
    slice.alongX = alongX;
    slice.width = 129;
    const double ground = region.groundAt(x, z);
    const double above = std::ceil((ground + 24.0 - y) / slice.metersPerPixel);
    slice.top = y + (above + 0.5) * slice.metersPerPixel;
    const double bottom = ground - region.field().params().maxDepth - 24.0;
    slice.height = static_cast<int>(std::ceil((slice.top - bottom) / slice.metersPerPixel));
    return slice;
}

// Row of the pixel whose center is at height y.
int rowAt(const CaveSlice& slice, double y) {
    return static_cast<int>(std::lround((slice.top - y) / slice.metersPerPixel - 0.5));
}

// Sampler over a box: the line of a section (with a meter around) from its bottom to its top, or
// the samples of a row of the map.
CaveSampler sliceSampler(const CaveRegion& region, const CaveSlice& slice) {
    const Point first = pointOf(slice, 0);
    const Point last = pointOf(slice, slice.width - 1);
    return region.sampler({std::min(first.x, last.x) - 1.0,
                           slice.top - slice.height * slice.metersPerPixel,
                           std::min(first.z, last.z) - 1.0, std::max(first.x, last.x) + 1.0,
                           slice.top, std::max(first.z, last.z) + 1.0});
}

CaveSampler rowSampler(const CaveRegion& region, int row) {
    const terrain::ErodedRegion& eroded = region.eroded();
    const int last = eroded.region().size - 1;
    double lowest = std::numeric_limits<double>::max();
    double highest = std::numeric_limits<double>::lowest();
    for (int column = 0; column <= last; ++column) {
        const double ground = region.groundAt(eroded.sampleX(column), eroded.sampleZ(row));
        lowest = std::min(lowest, ground);
        highest = std::max(highest, ground);
    }
    return region.sampler({eroded.sampleX(0), lowest - region.field().params().maxDepth - 8.0,
                           eroded.sampleZ(row), eroded.sampleX(last), highest + 8.0,
                           eroded.sampleZ(row)});
}

// What the plain scan of a column finds, every 4 m of depth from just under the minimum roof to
// just under the maximum depth: the shallowest cave, and the first water of a lake.
struct Probe {
    double depth = -1.0;
    bool water = false;
    bool frozen = false;
    double waterY = 0.0;
};

Probe probeColumn(CaveSampler& sampler, double x, double z, float ground) {
    const terrain::CaveParams& params = sampler.field().params();
    const CaveColumn column = sampler.column(x, z, ground);
    Probe probe;
    for (double depth = params.minRoof - 1.0; depth <= params.maxDepth + 1.0 && !probe.water;
         depth += 4.0) {
        const double y = static_cast<double>(ground) - depth;
        const auto terrain = static_cast<float>(-depth);
        const float density = sampler.density(column, y, terrain);
        if (density >= 0.0f) {
            probe.depth = probe.depth < 0.0 ? depth : probe.depth;
            probe.water = sampler.waterDensity(column, y, terrain, density) < 0.0f;
            probe.frozen = probe.water && column.frozen;
            probe.waterY = y;
        }
    }
    return probe;
}

// A section through the first sample of the map, row by row, with a lake in its column, at the
// height of a point of its water.
std::optional<CaveSlice> sliceThroughLake(const CaveRegion& region) {
    const terrain::ErodedRegion& eroded = region.eroded();
    for (int row = 0; row < eroded.region().size; ++row) {
        CaveSampler sampler = rowSampler(region, row);
        for (int column = 0; column < eroded.region().size; ++column) {
            const double x = eroded.sampleX(column);
            const double z = eroded.sampleZ(row);
            const Probe probe = probeColumn(sampler, x, z, region.groundAt(x, z));
            if (probe.water) {
                return sliceThrough(region, x, probe.waterY, z, true);
            }
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------

// Every pixel of the section under the ground is what the density says: rock (the line of the
// ground and the walls included) or carved air, which is the water (or the ice) of a lake where
// waterDensity is negative, else dry air or a speleothem. Returns the number of water pixels.
int checkAgainstDensity(const CaveRegion& region, const CaveSlice& slice, const Image& image,
                        bool frozen) {
    CaveSampler sampler = sliceSampler(region, slice);
    bool ok = true;
    int waterPixels = 0;
    for (int column = 0; column < slice.width; ++column) {
        const Point point = pointOf(slice, column);
        const float ground = region.groundAt(point.x, point.z);
        const CaveColumn caves = sampler.column(point.x, point.z, ground);
        for (int row = 0; row < slice.height; ++row) {
            const double y = rowHeight(slice, row);
            const Rgb color = pixel(image, column, row);
            const auto terrain = static_cast<float>(y - static_cast<double>(ground));
            if (y >= ground) {
                continue;
            }
            const float density = sampler.density(caves, y, terrain);
            if (density < 0.0f) {
                ok = ok && !isCarved(color);
            } else if (sampler.waterDensity(caves, y, terrain, density) < 0.0f) {
                ok = ok && color == rgb(frozen ? terrain::kCaveIceRgb : terrain::kCaveWaterRgb);
                ++waterPixels;
            } else {
                ok = ok && (isDryAir(color) || isSpeleothem(color));
            }
        }
    }
    TEST_CHECK(ok);
    return waterPixels;
}

// Air (dry, under water, or with a speleothem) within kCaveWallReach pixels (Euclidean).
bool airNear(const Image& image, int column, int row) {
    constexpr int reach = terrain::kCaveWallReach;
    for (int dy = -reach; dy <= reach; ++dy) {
        for (int dx = -reach; dx <= reach; ++dx) {
            const int c = column + dx;
            const int r = row + dy;
            if (dx * dx + dy * dy <= reach * reach && c >= 0 && r >= 0 && c < image.width &&
                r < image.height && isCarved(pixel(image, c, r))) {
                return true;
            }
        }
    }
    return false;
}

// Rock pixels with air around them have the color of a material, the others the shade of the
// rock; the material is the one of caveMaterial at the point whichever the normal (depth below
// the ground, environment above, height over the water of the aquifer, frozen or not); the rock
// right under water is the mud of the shore, or the ice of a frozen lake.
void checkWalls(const CaveRegion& region, const CaveSlice& slice, const Image& image,
                bool frozen) {
    CaveSampler sampler = sliceSampler(region, slice);
    const terrain::CaveMaterialParams& params = region.field().params().materials;
    const Rgb shore = materialColor(frozen ? CaveMaterial::Ice : CaveMaterial::Mud);
    bool ok = true;
    int count = 0;
    int beds = 0;
    for (int column = 0; column < slice.width; ++column) {
        const Point point = pointOf(slice, column);
        const float ground = region.groundAt(point.x, point.z);
        const terrain::AquiferCell& aquifer = sampler.aquiferAt(point.x, point.z);
        for (int row = 0; row < slice.height; ++row) {
            const Rgb color = pixel(image, column, row);
            const double y = rowHeight(slice, row);
            if (isCarved(color) || y >= ground || ground - y <= slice.metersPerPixel) {
                continue;
            }
            const bool wall = airNear(image, column, row);
            ok = ok && isRock(color) != wall && isMaterial(color) == wall;
            if (row > 0 && isFluid(pixel(image, column, row - 1))) {
                ok = ok && color == shore;
                ++beds;
            }
            if (!wall || ++count % 3 != 0) {
                continue;
            }
            terrain::CaveWallSite site;
            site.environment = region.environment().at(point.x, point.z);
            site.depth = static_cast<float>(static_cast<double>(ground) - y);
            site.aboveWater = aquifer.wet ? static_cast<float>(y - aquifer.level) : 1.0e9f;
            site.frozenWater = aquifer.wet && aquifer.frozen;
            bool known = false;
            for (const float normal : {-1.0f, 0.0f, 1.0f}) {
                site.normalY = normal;
                known = known || color == materialColor(terrain::caveMaterial(site, params));
            }
            ok = ok && known;
        }
    }
    TEST_CHECK(count > 100 && beds > 0 && ok);
}

// Section through a lake: the water (or the ice) of its pixels against the density, the walls,
// the stats, and the same pixels when rendered again.
void testLakeSlice(const CaveRegion& region, bool frozen) {
    const std::optional<CaveSlice> found = sliceThroughLake(region);
    TEST_CHECK(found.has_value());
    if (!found) {
        return;
    }
    const CaveSlice& slice = *found;
    terrain::CaveSliceStats stats;
    const Image image = terrain::renderCaveSlice(region, slice, &stats);
    TEST_CHECK(image.width == 129 && image.height == slice.height);
    const int waterPixels = checkAgainstDensity(region, slice, image, frozen);
    TEST_CHECK(waterPixels > 1);
    TEST_CHECK(stats.waterFraction ==
               static_cast<double>(waterPixels) / static_cast<double>(image.width * image.height));
    checkWalls(region, slice, image, frozen);
    TEST_CHECK(terrain::renderCaveSlice(region, slice).pixels == image.pixels);
}

// Speleothems of the plane of a section, from an independent placement over its rectangle.
Speleothems speleothemsOf(const CaveRegion& region, const CaveSlice& slice) {
    const double along = slice.width * slice.metersPerPixel / 2.0;
    const double across = terrain::kCaveSpeleothemReach;
    const double rx = slice.alongX ? along : across;
    const double rz = slice.alongX ? across : along;
    double lowest = std::numeric_limits<double>::max();
    double highest = std::numeric_limits<double>::lowest();
    for (double x = -rx; x <= rx; x += slice.alongX ? 1.0 : across) {
        for (double z = -rz; z <= rz; z += slice.alongX ? across : 1.0) {
            const double ground = region.groundAt(slice.centerX + x, slice.centerZ + z);
            lowest = std::min(lowest, ground);
            highest = std::max(highest, ground);
        }
    }
    CaveSampler sampler = region.sampler(
        {slice.centerX - rx - 1.0, lowest - region.field().params().maxDepth - 16.0,
         slice.centerZ - rz - 1.0, slice.centerX + rx + 1.0, highest + 16.0,
         slice.centerZ + rz + 1.0});
    return region.speleothems().place(sampler, slice.centerX - rx, slice.centerZ - rz,
                                      slice.centerX + rx, slice.centerZ + rz);
}

// The first speleothem at least 1.5 m long in the 64 m windows along the line through the center.
std::optional<Speleothem> findSpeleothem(const CaveRegion& region) {
    for (int window = -4; window <= 4; ++window) {
        const double x = kArea.centerX + 64.0 * window;
        const double z = kArea.centerZ;
        const CaveSlice slice = sliceThrough(region, x, region.groundAt(x, z) - 20.0, z, true);
        for (const Speleothem& s : speleothemsOf(region, slice)) {
            if (s.length >= 1.5f) {
                return s;
            }
        }
    }
    return std::nullopt;
}

double tipOf(const Speleothem& s) {
    return s.kind == SpeleothemKind::Stalactite ? s.y - s.length : s.y + s.length;
}

// The pixel lies in the box of a placed speleothem (a pixel more around).
bool nearSpeleothem(const CaveSlice& slice, int column, int row, const Speleothems& all) {
    const Point point = pointOf(slice, column);
    const double pixelSize = slice.metersPerPixel;
    const double y = rowHeight(slice, row);
    return std::any_of(all.begin(), all.end(), [&](const Speleothem& s) {
        const double across = slice.alongX ? point.x - s.x : point.z - s.z;
        return std::abs(across) <= s.radius + pixelSize &&
               y >= std::min<double>(s.y, tipOf(s)) - pixelSize &&
               y <= std::max<double>(s.y, tipOf(s)) + pixelSize;
    });
}

// Section through the middle of a speleothem along an axis: its pixel in the middle column, at the
// middle of its length, has its color; the stats count the speleothems placed over the plane;
// the pixels of the color lie in the box of one of them.
void checkSpeleothemSlice(const CaveRegion& region, const Speleothem& s, bool alongX) {
    const double middle = (s.y + tipOf(s)) / 2.0;
    const CaveSlice slice = sliceThrough(region, s.x, middle, s.z, alongX);
    terrain::CaveSliceStats stats;
    const Image image = terrain::renderCaveSlice(region, slice, &stats);
    const Rgb color = rgb(s.ice ? terrain::kCaveSpeleothemIceRgb : terrain::kCaveSpeleothemRgb);
    TEST_CHECK(pixel(image, 64, rowAt(slice, middle)) == color);
    const Speleothems placed = speleothemsOf(region, slice);
    TEST_CHECK(!placed.empty() && stats.speleothems == placed.size());
    bool stray = false;
    for (int row = 0; row < image.height; ++row) {
        for (int column = 0; column < image.width; ++column) {
            stray = stray || (isSpeleothem(pixel(image, column, row)) &&
                              !nearSpeleothem(slice, column, row, placed));
        }
    }
    TEST_CHECK(!stray);
    TEST_CHECK(terrain::renderCaveSlice(region, slice).pixels == image.pixels);
}

void testSpeleothems(const CaveRegion& region) {
    const std::optional<Speleothem> speleothem = findSpeleothem(region);
    TEST_CHECK(speleothem.has_value());
    for (const bool alongX : {true, false}) {
        if (speleothem) {
            checkSpeleothemSlice(region, *speleothem, alongX);
        }
    }
}

// A sample of the map has the color of the relief mixed 60% with the tint of the depth of its
// shallowest cave (from the shallow to the deep tint over the scanned depths), and over a lake
// that mixed 70% with the water or the ice. Without a cave, the color of the relief.
bool previewMatches(Rgb base, Rgb color, const Probe& probe, const terrain::CaveParams& params) {
    const double first = params.minRoof - 1.0;
    const double t = std::clamp((probe.depth - first) / (params.maxDepth + 1.0 - first), 0.0, 1.0);
    const std::uint8_t from[3] = {base.r, base.g, base.b};
    const std::uint8_t to[3] = {color.r, color.g, color.b};
    bool matches = true;
    for (std::size_t c = 0; c < 3; ++c) {
        double value = from[c];
        if (probe.depth >= 0.0) {
            const double tint = terrain::kCaveShallowRgb[c] +
                                (terrain::kCaveDeepRgb[c] - terrain::kCaveShallowRgb[c]) * t;
            value += (tint - value) * 0.6;
        }
        if (probe.water) {
            const double lake = probe.frozen ? terrain::kCaveIceRgb[c] : terrain::kCaveWaterRgb[c];
            value += (lake - value) * 0.7;
        }
        matches = matches && std::abs(value - to[c]) <= 1.0;
    }
    return matches;
}

// The map: the colors above (but for rivers, entrances and the line of the section), and the
// lakes and caves counted by the stats, against a plain scan of every sample.
void testPreviewLakes(const CaveRegion& region, bool frozen) {
    terrain::CavePreviewStats stats;
    const Image preview = terrain::renderCavePreview(region, &stats);
    const terrain::ErodedRegion& eroded = region.eroded();
    const int size = eroded.region().size;
    const Image base = terrain::renderHeightmapPreview(eroded.grid());
    long lakes = 0;
    long caves = 0;
    bool ok = true;
    for (int row = 0; row < size; ++row) {
        CaveSampler sampler = rowSampler(region, row);
        for (int column = 0; column < size; ++column) {
            const double x = eroded.sampleX(column);
            const double z = eroded.sampleZ(row);
            const Probe probe = probeColumn(sampler, x, z, region.groundAt(x, z));
            lakes += probe.water ? 1 : 0;
            caves += probe.depth >= 0.0 ? 1 : 0;
            ok = ok && (!probe.water || probe.frozen == frozen);
            const Rgb color = pixel(preview, column, row);
            if (color != rgb(terrain::kCaveEntranceMarkRgb) &&
                color != rgb(terrain::kCaveSliceLineRgb) &&
                !eroded.rivers().isRiver(row * size + column)) {
                ok = ok && previewMatches(pixel(base, column, row), color, probe,
                                          region.field().params());
            }
        }
    }
    TEST_CHECK(lakes > 0 && lakes == std::lround(stats.lakeFraction * size * size));
    TEST_CHECK(caves == std::lround(stats.cavedFraction * size * size));
    TEST_CHECK(stats.lakeFraction <= stats.cavedFraction && ok);
    TEST_CHECK(terrain::renderCavePreview(region).pixels == preview.pixels);
}

} // namespace

int main() {
    testLakeSlice(warmSite(), false);
    testLakeSlice(coldSite(), true);
    testSpeleothems(warmSite());
    testSpeleothems(coldSite());
    testPreviewLakes(warmSite(), false);
    testPreviewLakes(coldSite(), true);
    return test::result();
}

#include "terrain/io/caves_png.hpp"

#include "terrain/generation/cave_entrances.hpp"
#include "terrain/io/heightmap_png.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <system_error>
#include <vector>

namespace terrain {

namespace {

using Rgb = std::span<const std::uint8_t, 3>;

// A section reaches this far (meters) over the highest ground and under the deepest cave.
constexpr double kSliceMargin = 16.0;
constexpr int kMaxSliceHeight = 640;
// The section follows the entrance nearest the center among those whose tube rises or falls
// by at most this much (the tangent of 22 degrees) across the axis it is closest to.
constexpr double kMaxTubeTilt = 0.4;

// Rock at the bottom of a section has this share less of the brightness it has at the ground.
constexpr float kRockDarkening = 0.6f;
// Samples of the map are scanned for caves every kScanStep meters of depth, in chunks of
// kScanChunk samples that each get a sampler (a small node cache, a tight range of heights).
constexpr double kScanStep = 4.0;
constexpr int kScanChunk = 32;
// Share of the tint in the color of a sample of the map over a cave.
constexpr float kTintShare = 0.6f;
// Smallest radius, in pixels, of the tube and of the chamber of an entrance on the map: more
// than half a diagonal of a pixel, so that an entrance always covers a sample.
constexpr double kMinMarkRadius = 0.75;

struct LinePoint {
    double x;
    double z;
};

std::uint8_t mix(std::uint8_t a, std::uint8_t b, float t) {
    return static_cast<std::uint8_t>(std::lround(a + (b - a) * t));
}

void setPixel(Image& image, int column, int row, Rgb rgb) {
    if (column < 0 || row < 0 || column >= image.width || row >= image.height) {
        return;
    }
    const auto offset = static_cast<std::size_t>((row * image.width + column) * 3);
    std::copy(rgb.begin(), rgb.end(), image.pixels.begin() + static_cast<std::ptrdiff_t>(offset));
}

// Position along the line of a section of a pixel column (fractions of a pixel are allowed).
double sliceU(const CaveSlice& slice, double column) {
    const double center = slice.alongX ? slice.centerX : slice.centerZ;
    return center + (column + 0.5 - slice.width / 2.0) * slice.metersPerPixel;
}

// Point of the world at the position u along the line of a section.
LinePoint slicePoint(const CaveSlice& slice, double u) {
    return slice.alongX ? LinePoint{u, slice.centerZ} : LinePoint{slice.centerX, u};
}

// Entrances whose reach meets the region.
std::vector<CaveEntrance> regionEntrances(const CaveRegion& region) {
    const HeightmapRegion& area = region.eroded().region();
    const double half = area.size * area.metersPerPixel / 2.0;
    return region.field().entrancesNear(area.centerX - half, area.centerZ - half,
                                        area.centerX + half, area.centerZ + half,
                                        region.ground());
}

bool siteInside(const HeightmapRegion& area, const CaveEntrance& entrance) {
    const double half = area.size * area.metersPerPixel / 2.0;
    return std::abs(entrance.topX - area.centerX) <= half &&
           std::abs(entrance.topZ - area.centerZ) <= half;
}

// Rise of the tube across the horizontal axis it follows least, over its run along the other.
double tubeTilt(const CaveEntrance& entrance) {
    const double dx = std::abs(entrance.bottomX - entrance.topX);
    const double dz = std::abs(entrance.bottomZ - entrance.topZ);
    return std::min(dx, dz) / std::max(dx, dz);
}

// The entrance nearest the center of the region among those that satisfy keep.
template <class Keep>
const CaveEntrance* nearestEntrance(const std::vector<CaveEntrance>& entrances,
                                    const HeightmapRegion& area, Keep keep) {
    const CaveEntrance* best = nullptr;
    double bestDistance = 0.0;
    for (const CaveEntrance& entrance : entrances) {
        const double distance = std::hypot(entrance.topX - area.centerX,
                                           entrance.topZ - area.centerZ);
        if (keep(entrance) && (best == nullptr || distance < bestDistance)) {
            best = &entrance;
            bestDistance = distance;
        }
    }
    return best;
}

// Through the middle of the tube, along the axis it runs closest to.
void aimAt(CaveSlice& slice, const CaveEntrance& entrance) {
    slice.alongX = std::abs(entrance.bottomX - entrance.topX) >=
                   std::abs(entrance.bottomZ - entrance.topZ);
    slice.centerX = (entrance.topX + entrance.bottomX) / 2.0;
    slice.centerZ = (entrance.topZ + entrance.bottomZ) / 2.0;
}

// Top and height from the ground along the line.
void fitHeight(const CaveRegion& region, CaveSlice& slice) {
    double lowest = std::numeric_limits<double>::max();
    double highest = std::numeric_limits<double>::lowest();
    for (int column = 0; column < slice.width; ++column) {
        const LinePoint point = slicePoint(slice, sliceU(slice, column));
        const double ground = region.groundAt(point.x, point.z);
        lowest = std::min(lowest, ground);
        highest = std::max(highest, ground);
    }
    slice.top = highest + kSliceMargin;
    const double bottom = lowest - region.field().params().maxDepth - kSliceMargin;
    const double rows = std::ceil((slice.top - bottom) / slice.metersPerPixel);
    slice.height = static_cast<int>(std::clamp(rows, 1.0, static_cast<double>(kMaxSliceHeight)));
}

CaveSlice defaultSlice(const CaveRegion& region, const std::vector<CaveEntrance>& entrances) {
    const HeightmapRegion& area = region.eroded().region();
    const auto inside = [&](const CaveEntrance& e) { return siteInside(area, e); };
    CaveSlice slice;
    slice.centerX = area.centerX;
    slice.centerZ = area.centerZ;
    const CaveEntrance* entrance =
        nearestEntrance(entrances, area, [&](const CaveEntrance& e) {
            return inside(e) && tubeTilt(e) <= kMaxTubeTilt;
        });
    if (entrance == nullptr) {
        entrance = nearestEntrance(entrances, area, inside);
    }
    if (entrance != nullptr) {
        aimAt(slice, *entrance);
    }
    fitHeight(region, slice);
    return slice;
}

// Color of the air of the component whose void is the smallest, limits included.
Rgb airColor(const CaveVoids& voids) {
    Rgb color = kEntranceAirRgb;
    float smallest = std::max(voids.entrances, voids.entranceLimit);
    const auto consider = [&](float value, Rgb rgb) {
        value = std::max(value, voids.limit);
        if (value < smallest) {
            smallest = value;
            color = rgb;
        }
    };
    consider(voids.spaghetti, kSpaghettiAirRgb);
    consider(voids.noodles, kNoodleAirRgb);
    consider(voids.cheese, kCheeseAirRgb);
    consider(voids.ravines, kRavineAirRgb);
    return color;
}

void setRockPixel(Image& image, int column, int row, double depth, const CaveParams& params) {
    const double range = params.maxDepth + 2.0 * kSliceMargin;
    const auto fade = static_cast<float>(std::clamp(depth / range, 0.0, 1.0));
    const float light = 1.0f - kRockDarkening * fade;
    std::uint8_t rgb[3];
    for (std::size_t c = 0; c < 3; ++c) {
        rgb[c] = static_cast<std::uint8_t>(std::lround(kCaveRockRgb[c] * light));
    }
    setPixel(image, column, row, rgb);
}

// A column of a section: the state of the caves in it and what its ground is.
struct SliceColumn {
    CaveColumn caves;
    bool river = false;
};

void paintColumn(Image& image, CaveSampler& sampler, const CaveSlice& slice,
                 const SliceColumn& column, int pixelColumn) {
    const CaveParams& params = sampler.field().params();
    const double ground = column.caves.ground;
    for (int row = 0; row < slice.height; ++row) {
        const double y = slice.top - (row + 0.5) * slice.metersPerPixel;
        if (y >= ground) {
            setPixel(image, pixelColumn, row, y < kSeaLevel ? kCaveSeaRgb : kCaveSkyRgb);
            continue;
        }
        const auto terrain = static_cast<float>(y - ground);
        if (sampler.density(column.caves, y, terrain) >= 0.0f) {
            setPixel(image, pixelColumn, row, airColor(sampler.voids(column.caves, y)));
        } else if (ground - y <= slice.metersPerPixel) {
            setPixel(image, pixelColumn, row, column.river ? kCaveRiverRgb : kCaveGroundRgb);
        } else {
            setRockPixel(image, pixelColumn, row, ground - y, params);
        }
    }
}

// Box of a section: the line over the heights of its rows.
CaveBox sliceBox(const CaveSlice& slice) {
    const LinePoint first = slicePoint(slice, sliceU(slice, 0));
    const LinePoint last = slicePoint(slice, sliceU(slice, slice.width - 1));
    return {std::min(first.x, last.x), slice.top - slice.height * slice.metersPerPixel,
            std::min(first.z, last.z), std::max(first.x, last.x), slice.top,
            std::max(first.z, last.z)};
}

void drawRivers(Image& image, const RiverNetwork& rivers) {
    for (int i = 0; i < rivers.size * rivers.size; ++i) {
        if (rivers.isRiver(i)) {
            setPixel(image, i % rivers.size, i / rivers.size, kCaveRiverRgb);
        }
    }
}

// Depth of the shallowest carved air under the sample (x, z), or -1 if the scan finds none.
double shallowestCave(CaveSampler& sampler, double x, double z, float ground) {
    const CaveParams& params = sampler.field().params();
    const CaveColumn column = sampler.column(x, z, ground);
    for (double depth = params.minRoof - 1.0; depth <= params.maxDepth + 1.0;
         depth += kScanStep) {
        const double y = static_cast<double>(ground) - depth;
        if (sampler.density(column, y, static_cast<float>(-depth)) >= 0.0f) {
            return depth;
        }
    }
    return -1.0;
}

// Scans the samples [first, last] of a row of the map into depths (one per sample of the map).
void scanChunk(const CaveRegion& region, int row, int first, int last,
               std::vector<double>& depths) {
    const ErodedRegion& eroded = region.eroded();
    const CaveParams& params = region.field().params();
    const double z = eroded.sampleZ(row);
    std::vector<float> grounds;
    grounds.reserve(static_cast<std::size_t>(last - first + 1));
    float lowest = std::numeric_limits<float>::max();
    float highest = std::numeric_limits<float>::lowest();
    for (int column = first; column <= last; ++column) {
        grounds.push_back(region.groundAt(eroded.sampleX(column), z));
        lowest = std::min(lowest, grounds.back());
        highest = std::max(highest, grounds.back());
    }
    const CaveBox box{eroded.sampleX(first), static_cast<double>(lowest) - params.maxDepth - 8.0,
                      z, eroded.sampleX(last), static_cast<double>(highest) + 8.0, z};
    CaveSampler sampler = region.sampler(box);
    const int size = eroded.region().size;
    for (int column = first; column <= last; ++column) {
        const float ground = grounds[static_cast<std::size_t>(column - first)];
        depths[static_cast<std::size_t>(row * size + column)] =
            shallowestCave(sampler, eroded.sampleX(column), z, ground);
    }
}

std::vector<double> scanCaves(const CaveRegion& region) {
    const int size = region.eroded().region().size;
    std::vector<double> depths(static_cast<std::size_t>(size) * static_cast<std::size_t>(size),
                               -1.0);
    for (int row = 0; row < size; ++row) {
        for (int first = 0; first < size; first += kScanChunk) {
            scanChunk(region, row, first, std::min(first + kScanChunk, size) - 1, depths);
        }
    }
    return depths;
}

// Tints the samples over a cave, from the shallow to the deep color with the depth.
void tintCaves(Image& image, const std::vector<double>& depths, const CaveParams& params) {
    const double first = params.minRoof - 1.0;
    const double span = params.maxDepth + 1.0 - first;
    for (std::size_t i = 0; i < depths.size(); ++i) {
        if (depths[i] < 0.0) {
            continue;
        }
        const auto t = static_cast<float>(std::clamp((depths[i] - first) / span, 0.0, 1.0));
        for (std::size_t c = 0; c < 3; ++c) {
            const std::uint8_t tint = mix(kCaveShallowRgb[c], kCaveDeepRgb[c], t);
            image.pixels[i * 3 + c] = mix(image.pixels[i * 3 + c], tint, kTintShare);
        }
    }
}

// Pixel of the map nearest to a point of the world.
struct PixelPoint {
    int column;
    int row;
};

PixelPoint nearestPixel(const ErodedRegion& eroded, double x, double z) {
    const double pixel = eroded.region().metersPerPixel;
    return {static_cast<int>(std::lround((x - eroded.sampleX(0)) / pixel)),
            static_cast<int>(std::lround((z - eroded.sampleZ(0)) / pixel))};
}

// Range of pixels [first, last] along an axis of the map that covers meters [low, high].
struct PixelRange {
    int first;
    int last;
};

PixelRange pixelRange(double low, double high, double origin, double pixel, int size) {
    return {std::max(static_cast<int>(std::floor((low - origin) / pixel)), 0),
            std::min(static_cast<int>(std::ceil((high - origin) / pixel)), size - 1)};
}

// Pixels whose center lies within the radius of the tube of the axis of the entrance, or within
// the radius of its chamber of the bottom, seen from above.
void drawEntrance(Image& image, const ErodedRegion& eroded, const CaveEntrance& entrance) {
    const double pixel = eroded.region().metersPerPixel;
    const double tube = std::max<double>(entrance.tubeRadius, kMinMarkRadius * pixel);
    const double chamber = std::max<double>(entrance.chamberRadius, kMinMarkRadius * pixel);
    const double reach = std::max(tube, chamber);
    const int size = eroded.region().size;
    const double minX = std::min(entrance.topX, entrance.bottomX) - reach;
    const double maxX = std::max(entrance.topX, entrance.bottomX) + reach;
    const double minZ = std::min(entrance.topZ, entrance.bottomZ) - reach;
    const double maxZ = std::max(entrance.topZ, entrance.bottomZ) + reach;
    const PixelRange columns = pixelRange(minX, maxX, eroded.sampleX(0), pixel, size);
    const PixelRange rows = pixelRange(minZ, maxZ, eroded.sampleZ(0), pixel, size);
    for (int row = rows.first; row <= rows.last; ++row) {
        for (int column = columns.first; column <= columns.last; ++column) {
            const double x = eroded.sampleX(column);
            const double z = eroded.sampleZ(row);
            if (entranceAxisDistance(entrance, x, z) <= tube ||
                std::hypot(x - entrance.bottomX, z - entrance.bottomZ) <= chamber) {
                setPixel(image, column, row, kCaveEntranceMarkRgb);
            }
        }
    }
}

// The line of a section, one pixel thick: points every half pixel along it.
void drawSliceLine(Image& image, const ErodedRegion& eroded, const CaveSlice& slice) {
    const double pixel = eroded.region().metersPerPixel;
    const int steps = static_cast<int>(std::ceil(2.0 * slice.width * slice.metersPerPixel / pixel));
    for (int i = 0; i <= steps; ++i) {
        const double position = slice.width * static_cast<double>(i) / steps - 0.5;
        const LinePoint point = slicePoint(slice, sliceU(slice, position));
        const PixelPoint at = nearestPixel(eroded, point.x, point.z);
        setPixel(image, at.column, at.row, kCaveSliceLineRgb);
    }
}

} // namespace

CaveSlice defaultCaveSlice(const CaveRegion& region) {
    return defaultSlice(region, regionEntrances(region));
}

Image renderCaveSlice(const CaveRegion& region, const CaveSlice& slice) {
    const auto samples =
        static_cast<std::size_t>(slice.width) * static_cast<std::size_t>(slice.height);
    Image image{slice.width, slice.height, 3, std::vector<std::uint8_t>(samples * 3, 0)};
    CaveSampler sampler = region.sampler(sliceBox(slice));
    const float riverReach = static_cast<float>(region.eroded().region().metersPerPixel) / 2.0f;
    for (int column = 0; column < slice.width; ++column) {
        const LinePoint point = slicePoint(slice, sliceU(slice, column));
        const float ground = region.groundAt(point.x, point.z);
        SliceColumn state;
        state.caves = sampler.column(point.x, point.z, ground);
        state.river = region.riverDistanceAt(point.x, point.z) < riverReach;
        paintColumn(image, sampler, slice, state, column);
    }
    return image;
}

Image renderCavePreview(const CaveRegion& region, CavePreviewStats* stats) {
    const ErodedRegion& eroded = region.eroded();
    Image image = renderHeightmapPreview(eroded.grid());
    drawRivers(image, eroded.rivers());
    const std::vector<double> depths = scanCaves(region);
    tintCaves(image, depths, region.field().params());

    const std::vector<CaveEntrance> entrances = regionEntrances(region);
    for (const CaveEntrance& entrance : entrances) {
        drawEntrance(image, eroded, entrance);
    }
    drawSliceLine(image, eroded, defaultSlice(region, entrances));
    if (stats != nullptr) {
        const auto inside =
            std::count_if(entrances.begin(), entrances.end(), [&](const CaveEntrance& e) {
                return siteInside(eroded.region(), e);
            });
        const auto caved = std::count_if(depths.begin(), depths.end(),
                                         [](double depth) { return depth >= 0.0; });
        stats->entrances = static_cast<std::size_t>(inside);
        stats->cavedFraction = static_cast<double>(caved) / static_cast<double>(depths.size());
    }
    return image;
}

bool exportCavePngs(const CaveRegion& region, const std::filesystem::path& directory,
                    CavePreviewStats* stats) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return false;
    }
    const bool slice =
        writePng(directory / "cave_slice.png", renderCaveSlice(region, defaultCaveSlice(region)));
    const bool preview = writePng(directory / "cave_preview.png", renderCavePreview(region, stats));
    return slice && preview;
}

} // namespace terrain

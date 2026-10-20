#include "terrain/io/caves_png.hpp"

#include "caves_png_layers.hpp"

#include "terrain/generation/cave_entrances.hpp"
#include "terrain/io/heightmap_png.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <system_error>
#include <vector>

namespace terrain {

namespace {

using cave_png::kSliceMargin;
using cave_png::LinePoint;
using cave_png::mix;
using cave_png::Scan;
using cave_png::setPixel;
using cave_png::sliceU;
using cave_png::slicePoint;

constexpr int kMaxSliceHeight = 640;
// The section follows the entrance nearest the center among those whose tube rises or falls
// by at most this much (the tangent of 22 degrees) across the axis it is closest to.
constexpr double kMaxTubeTilt = 0.4;

// Samples of the map are scanned for caves every kScanStep meters of depth, in chunks of
// kScanChunk samples that each get a sampler (a small node cache, a tight range of heights).
constexpr double kScanStep = 4.0;
constexpr int kScanChunk = 32;
// Share of the tint in the color of a sample of the map over a cave, and of the water or the ice
// of a lake in the color of a sample over one.
constexpr float kTintShare = 0.6f;
constexpr float kLakeShare = 0.7f;
// Smallest radius, in pixels, of the tube and of the chamber of an entrance on the map: more
// than half a diagonal of a pixel, so that an entrance always covers a sample.
constexpr double kMinMarkRadius = 0.75;

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

void drawRivers(Image& image, const RiverNetwork& rivers) {
    for (int i = 0; i < rivers.size * rivers.size; ++i) {
        if (rivers.isRiver(i)) {
            setPixel(image, i % rivers.size, i / rivers.size, kCaveRiverRgb);
        }
    }
}

// Depth of the first sample of the scan (the samples are first + n * kScanStep) at or under depth.
double scanDepthFrom(double first, double depth) {
    return first + std::ceil((depth - first) / kScanStep) * kScanStep;
}

// Scans the column from just under the minimum roof to just under the maximum depth: the
// shallowest carved air, and in a wet aquifer (the only one with a lake) whether the carved air
// of the samples under its level holds water. A dry column stops at its first cave. The
// galleries, rivers and lava are then looked for apart (scanNetwork).
Scan scanColumn(CaveSampler& sampler, double x, double z, float ground, double metersPerPixel) {
    const CaveParams& params = sampler.field().params();
    const CaveColumn column = sampler.column(x, z, ground);
    const bool wet = column.waterLevel > kDryLevel;
    const double first = params.minRoof - 1.0;
    Scan scan;
    for (double depth = first; depth <= params.maxDepth + 1.0; depth += kScanStep) {
        const double y = static_cast<double>(ground) - depth;
        const auto terrain = static_cast<float>(-depth);
        const float density = sampler.density(column, y, terrain);
        if (density < 0.0f) {
            continue;
        }
        if (scan.depth < 0.0) {
            scan.depth = depth;
        }
        if (!wet) {
            break;
        }
        if (sampler.waterDensity(column, y, terrain, density) < 0.0f) {
            scan.water = true;
            scan.frozen = column.frozen;
            break;
        }
        // Only the samples under the water level can hold water: go on from the first of them.
        const double level = static_cast<double>(ground) - column.waterLevel;
        depth = std::max(depth, scanDepthFrom(first, level) - kScanStep);
    }
    cave_png::scanNetwork(sampler, column, metersPerPixel, scan);
    return scan;
}

// Scans the samples [first, last] of a row of the map into scans (one per sample of the map).
void scanChunk(const CaveRegion& region, int row, int first, int last, std::vector<Scan>& scans) {
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
        scans[static_cast<std::size_t>(row * size + column)] =
            scanColumn(sampler, eroded.sampleX(column), z, ground,
                       eroded.region().metersPerPixel);
    }
}

std::vector<Scan> scanCaves(const CaveRegion& region) {
    const int size = region.eroded().region().size;
    std::vector<Scan> scans(static_cast<std::size_t>(size) * static_cast<std::size_t>(size));
    for (int row = 0; row < size; ++row) {
        for (int first = 0; first < size; first += kScanChunk) {
            scanChunk(region, row, first, std::min(first + kScanChunk, size) - 1, scans);
        }
    }
    return scans;
}

// Tints the samples over a cave toward caveTint, or from the shallow to the deep color with the
// depth without it, and the samples over a lake with the water or the ice over that.
void tintCaves(Image& image, const std::vector<Scan>& scans, const CaveParams& params,
               const cave_png::CaveTint& caveTint) {
    const double first = params.minRoof - 1.0;
    const double span = params.maxDepth + 1.0 - first;
    for (std::size_t i = 0; i < scans.size(); ++i) {
        if (scans[i].depth < 0.0) {
            continue;
        }
        const auto t = static_cast<float>(std::clamp((scans[i].depth - first) / span, 0.0, 1.0));
        std::array<std::uint8_t, 3> tints{};
        for (std::size_t c = 0; c < 3; ++c) {
            tints[c] = mix(kCaveShallowRgb[c], kCaveDeepRgb[c], t);
        }
        if (caveTint) {
            tints = caveTint(i, t);
        }
        for (std::size_t c = 0; c < 3; ++c) {
            const std::uint8_t tint = tints[c];
            image.pixels[i * 3 + c] = mix(image.pixels[i * 3 + c], tint, kTintShare);
            if (scans[i].water) {
                const std::uint8_t lake = scans[i].frozen ? kCaveIceRgb[c] : kCaveWaterRgb[c];
                image.pixels[i * 3 + c] = mix(image.pixels[i * 3 + c], lake, kLakeShare);
            }
        }
    }
}

// Shares of the samples of the map with a cave, a lake, a gallery, a river and lava.
void fillScanStats(CavePreviewStats& stats, const std::vector<Scan>& scans) {
    const auto share = [&scans](auto has) {
        return static_cast<double>(std::count_if(scans.begin(), scans.end(), has)) /
               static_cast<double>(scans.size());
    };
    stats.cavedFraction = share([](const Scan& scan) { return scan.depth >= 0.0; });
    stats.lakeFraction = share([](const Scan& scan) { return scan.water; });
    stats.galleryFraction = share([](const Scan& scan) { return scan.gallery; });
    stats.riverFraction = share([](const Scan& scan) { return scan.river; });
    stats.lavaFraction = share([](const Scan& scan) { return scan.lava; });
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

namespace cave_png {

Image renderCaveMap(const CaveRegion& region, const std::function<void(Image&)>& beforeCaves,
                    const CaveTint& caveTint, bool withSliceLine, CavePreviewStats* stats) {
    const ErodedRegion& eroded = region.eroded();
    Image image = renderHeightmapPreview(eroded.grid());
    drawRivers(image, eroded.rivers());
    if (beforeCaves) {
        beforeCaves(image);
    }
    const std::vector<Scan> scans = scanCaves(region);
    tintCaves(image, scans, region.field().params(), caveTint);
    cave_png::drawNetwork(image, scans);

    const std::vector<CaveEntrance> entrances = regionEntrances(region);
    for (const CaveEntrance& entrance : entrances) {
        drawEntrance(image, eroded, entrance);
    }
    if (withSliceLine) {
        drawSliceLine(image, eroded, defaultSlice(region, entrances));
    }
    if (stats != nullptr) {
        const auto inside =
            std::count_if(entrances.begin(), entrances.end(), [&](const CaveEntrance& e) {
                return siteInside(eroded.region(), e);
            });
        stats->entrances = static_cast<std::size_t>(inside);
        fillScanStats(*stats, scans);
    }
    return image;
}

} // namespace cave_png

Image renderCavePreview(const CaveRegion& region, CavePreviewStats* stats) {
    return cave_png::renderCaveMap(region, {}, {}, true, stats);
}

bool exportCavePngs(const CaveRegion& region, const std::filesystem::path& directory,
                    CavePreviewStats* stats) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return false;
    }
    CaveSliceStats sliceStats;
    const bool slice = writePng(directory / "cave_slice.png",
                                renderCaveSlice(region, defaultCaveSlice(region), &sliceStats));
    const bool preview = writePng(directory / "cave_preview.png", renderCavePreview(region, stats));
    if (stats != nullptr) {
        stats->speleothems = sliceStats.speleothems;
    }
    return slice && preview;
}

} // namespace terrain

#include "terrain/io/cave_regions_png.hpp"

#include "caves_png_layers.hpp"

#include "terrain/det_math.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <span>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace terrain {

namespace {

using Color = std::array<float, 3>;
using TypeShares = std::array<double, kCaveDistrictTypeCount>;

// How far the hillshade darkens the colors of the map: 0 = not at all, 1 = the full Lambert
// factor of the relief.
constexpr float kShadeStrength = 0.6f;
// Light of the ground under the sea, and where it is too deep for caves.
constexpr float kSeaLight = 0.7f;
constexpr float kDeepLight = 0.35f;
// Light of the pixels on a border between two nearest districts.
constexpr float kBorderLight = 0.5f;

// Meetings: the sites and the midpoint of the pair stand this far over the sea, the two
// heaviest types weigh at least this much at the midpoint, and the cells are searched this
// far (in cells) around the point.
constexpr float kMeetingMinHeight = 10.0f;
constexpr float kMeetingMinWeight = 0.3f;
constexpr int kMeetingReach = 5;

struct Point {
    double x;
    double z;
};

// Point of the world sampled by a pixel of an area.
Point pixelPoint(const HeightmapRegion& area, int column, int row) {
    const double half = area.size / 2.0;
    return {area.centerX + (column + 0.5 - half) * area.metersPerPixel,
            area.centerZ + (row + 0.5 - half) * area.metersPerPixel};
}

std::size_t index(CaveDistrictType type) {
    return static_cast<std::size_t>(type);
}

void requireDistricts(const CaveField& field) {
    if (field.districts() == nullptr) {
        throw std::invalid_argument("cave regions png: the caves have no cave regions");
    }
}

std::uint8_t toByte(float value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 255.0f)));
}

// Sum of the weights times the colors of the districts at a point.
Color districtMix(const DistrictWeights& weights) {
    Color mix{};
    for (std::size_t i = 0; i < static_cast<std::size_t>(weights.count); ++i) {
        const auto& rgb = kCaveDistrictRgb[index(weights.districts[i].type)];
        for (std::size_t c = 0; c < mix.size(); ++c) {
            mix[c] += weights.weights[i] * static_cast<float>(rgb[c]);
        }
    }
    return mix;
}

void addShares(TypeShares& shares, const DistrictWeights& weights) {
    for (std::size_t i = 0; i < static_cast<std::size_t>(weights.count); ++i) {
        shares[index(weights.districts[i].type)] += static_cast<double>(weights.weights[i]);
    }
}

// Light of the relief at a pixel, never above 1 (flat ground, or facing the light): the Lambert
// factor of the ground normal lit from the north-west, 45 degrees high, kShadeStrength of the way
// from 1. Under the sea, 1.
float hillshade(const HeightGrid& grid, int column, int row) {
    if (grid.at(column, row) < kSeaLevel) {
        return 1.0f;
    }
    const int last = grid.size - 1;
    const int left = std::max(column - 1, 0);
    const int right = std::min(column + 1, last);
    const int up = std::max(row - 1, 0);
    const int down = std::min(row + 1, last);
    const double dx = (right - left) * grid.metersPerPixel;
    const double dz = (down - up) * grid.metersPerPixel;
    const double slopeX = dx > 0.0 ? (grid.at(right, row) - grid.at(left, row)) / dx : 0.0;
    const double slopeZ = dz > 0.0 ? (grid.at(column, down) - grid.at(column, up)) / dz : 0.0;
    const double lightAxis = 0.5;
    const double lightY = std::sqrt(0.5);
    const double lambert = (slopeX * lightAxis + lightY + slopeZ * lightAxis) /
                           std::sqrt(slopeX * slopeX + 1.0 + slopeZ * slopeZ);
    const auto light = static_cast<float>(std::clamp(0.35 + 0.65 * lambert / lightY, 0.2, 1.0));
    return 1.0f - kShadeStrength * (1.0f - light);
}

// Light of the ground by its height: full on land, less under the sea, least where caves are
// forbidden (deeper than the deep ocean limit).
float groundLight(float height, float deepOceanDepth) {
    if (height >= kSeaLevel) {
        return 1.0f;
    }
    return height > kSeaLevel - deepOceanDepth ? kSeaLight : kDeepLight;
}

bool cavesPossible(float height, float deepOceanDepth) {
    return height > kSeaLevel - deepOceanDepth;
}

// The district whose site is the nearest to a pixel.
struct CellId {
    int x = 0;
    int z = 0;

    friend bool operator==(const CellId&, const CellId&) = default;
};

// Heights of the pixels of an area, from the ground of the caves.
HeightGrid sampleGround(const CaveGround& ground, const HeightmapRegion& area) {
    HeightGrid grid{area.size, area.metersPerPixel, {}};
    grid.heights.reserve(static_cast<std::size_t>(area.size) * static_cast<std::size_t>(area.size));
    for (int row = 0; row < area.size; ++row) {
        for (int column = 0; column < area.size; ++column) {
            const Point at = pixelPoint(area, column, row);
            grid.heights.push_back(ground.height(at.x, at.z));
        }
    }
    return grid;
}

void scalePixel(Image& image, std::size_t pixel, float factor) {
    for (std::size_t c = 0; c < 3; ++c) {
        std::uint8_t& value = image.pixels[pixel * 3 + c];
        value = toByte(static_cast<float>(value) * factor);
    }
}

// Darkens the pixels whose nearest district is not that of their right or lower neighbor.
void darkenBorders(Image& image, const std::vector<CellId>& nearest) {
    const auto size = static_cast<std::size_t>(image.width);
    for (std::size_t row = 0; row < size; ++row) {
        for (std::size_t column = 0; column < size; ++column) {
            const std::size_t i = row * size + column;
            const bool right = column + 1 < size && nearest[i + 1] != nearest[i];
            const bool down = row + 1 < size && nearest[i + size] != nearest[i];
            if (right || down) {
                scalePixel(image, i, kBorderLight);
            }
        }
    }
}

// Square of the site of a district: outline and fill, clipped to the image.
void drawSiteMark(Image& image, int column, int row) {
    const int outline = kCaveRegionsSiteReach + 1;
    for (int dz = -outline; dz <= outline; ++dz) {
        for (int dx = -outline; dx <= outline; ++dx) {
            const bool edge = std::max(std::abs(dx), std::abs(dz)) == outline;
            cave_png::setPixel(image, column + dx, row + dz,
                               edge ? kCaveRegionsSiteOutlineRgb : kCaveRegionsSiteRgb);
        }
    }
}

// Marks the sites inside the map and returns how many there are. Sites move by a quarter of a
// cell at most, so the cells around the area hold all of them.
std::size_t drawSites(Image& image, AquiferCache& cache, const CaveDistricts& districts,
                      const HeightmapRegion& area) {
    const double cell = districts.params().cellSize;
    const double half = area.size * area.metersPerPixel / 2.0;
    const auto cellOf = [&](double meters) { return static_cast<int>(std::floor(meters / cell)); };
    std::size_t sites = 0;
    for (int cellZ = cellOf(area.centerZ - half) - 1; cellZ <= cellOf(area.centerZ + half) + 1;
         ++cellZ) {
        for (int cellX = cellOf(area.centerX - half) - 1; cellX <= cellOf(area.centerX + half) + 1;
             ++cellX) {
            const CaveDistrict& district = cache.district(cellX, cellZ);
            const auto column = static_cast<int>(
                std::floor((district.siteX - (area.centerX - half)) / area.metersPerPixel));
            const auto row = static_cast<int>(
                std::floor((district.siteZ - (area.centerZ - half)) / area.metersPerPixel));
            if (column >= 0 && row >= 0 && column < area.size && row < area.size) {
                drawSiteMark(image, column, row);
                ++sites;
            }
        }
    }
    return sites;
}

void requireArea(const HeightmapRegion& area) {
    if (area.size < 1 || !(area.metersPerPixel > 0.0) || !std::isfinite(area.metersPerPixel) ||
        !std::isfinite(area.centerX) || !std::isfinite(area.centerZ)) {
        throw std::invalid_argument("cave regions png: empty or non-finite area");
    }
}

// Sets the stats of the map from the sums of the weights over the pixels where caves are
// possible.
void fillMapStats(CaveRegionsMapStats& stats, TypeShares shares, std::size_t cavePixels,
                  std::size_t pixels, std::size_t sites) {
    stats.typesPresent = 0;
    for (std::size_t t = 0; t < shares.size(); ++t) {
        stats.typeShares[t] = cavePixels > 0 ? shares[t] / static_cast<double>(cavePixels) : 0.0;
        stats.typesPresent += stats.typeShares[t] > 0.0 ? 1 : 0;
    }
    stats.cavedGroundFraction = static_cast<double>(cavePixels) / static_cast<double>(pixels);
    stats.sites = sites;
}

// Parameters of the caves of the exports: the default cave regions.
CaveParams districtCaveParams() {
    CaveParams params;
    params.districts = CaveDistrictParams{};
    return params;
}

// Cell of the grid of the cave regions holding a point.
CellId cellAt(const CaveDistricts& districts, double x, double z) {
    const double cell = districts.params().cellSize;
    return {static_cast<int>(std::floor(x / cell)), static_cast<int>(std::floor(z / cell))};
}

// Weight of the second heaviest type at a point (the types of the districts added up).
float secondTypeWeight(const DistrictWeights& weights) {
    std::array<float, kCaveDistrictTypeCount> byType{};
    for (std::size_t i = 0; i < static_cast<std::size_t>(weights.count); ++i) {
        byType[index(weights.districts[i].type)] += weights.weights[i];
    }
    std::partial_sort(byType.begin(), byType.begin() + 2, byType.end(), std::greater<>());
    return byType[1];
}

// Midpoint of two neighbor sites of different types, if it is a meeting on land.
bool isMeeting(AquiferCache& cache, const CaveGround& ground, const CaveDistrict& a,
               const CaveDistrict& b, Point& midpoint) {
    midpoint = {(a.siteX + b.siteX) / 2.0, (a.siteZ + b.siteZ) / 2.0};
    const auto high = [&](double x, double z) { return ground.height(x, z) >= kMeetingMinHeight; };
    return a.type != b.type && high(midpoint.x, midpoint.z) && high(a.siteX, a.siteZ) &&
           high(b.siteX, b.siteZ) &&
           secondTypeWeight(cache.districtWeights(midpoint.x, midpoint.z)) >= kMeetingMinWeight;
}

// The faces of a cell that a pair of neighbors is searched across, each pair once.
struct Step {
    int x;
    int z;
};
constexpr std::array<Step, 4> kNeighborSteps = {{{1, 0}, {0, 1}, {1, 1}, {-1, 1}}};

// Whether either type of a pair of districts is among the preferred ones.
bool holdsPreferred(std::span<const CaveDistrictType> preferred, const CaveDistrict& a,
                    const CaveDistrict& b) {
    const auto has = [&](CaveDistrictType type) {
        return std::find(preferred.begin(), preferred.end(), type) != preferred.end();
    };
    return has(a.type) || has(b.type);
}

// The nearest meeting offered so far: the first on a tie.
struct NearestMeeting {
    CaveRegionsMeeting meeting;
    double distance = 0.0;

    void offer(const Point& at, double away) {
        if (!meeting.found || away < distance) {
            meeting = {at.x, at.z, true};
            distance = away;
        }
    }
};

// Tints each sample of the preview with the district mix of its point, kept in mixes, and adds
// the weights of the districts there to sums.
void tintDistricts(Image& image, const ErodedRegion& eroded, AquiferCache& cache,
                   TypeShares& sums, std::vector<Color>& mixes) {
    const int size = eroded.region().size;
    mixes.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size), Color{});
    for (int row = 0; row < size; ++row) {
        for (int column = 0; column < size; ++column) {
            const DistrictWeights weights =
                cache.districtWeights(eroded.sampleX(column), eroded.sampleZ(row));
            const Color tint = districtMix(weights);
            addShares(sums, weights);
            mixes[static_cast<std::size_t>(row * size + column)] = tint;
            const auto offset = static_cast<std::size_t>(row * size + column) * 3;
            for (std::size_t c = 0; c < 3; ++c) {
                const float relief = static_cast<float>(image.pixels[offset + c]);
                image.pixels[offset + c] =
                    toByte(relief + (tint[c] - relief) * kCaveRegionsPreviewTintShare);
            }
        }
    }
}

} // namespace

std::array<std::uint8_t, 3> caveDistrictRgb(CaveDistrictType type) {
    return kCaveDistrictRgb[index(type)];
}

Image renderCaveRegionsMap(const CaveField& field, const CaveGround& ground,
                           const HeightmapRegion& area, CaveRegionsMapStats* stats) {
    requireDistricts(field);
    requireArea(area);
    const HeightGrid heights = sampleGround(ground, area);
    const float deepOceanDepth = field.params().deepOceanDepth;
    AquiferCache cache(field, ground);
    const auto pixels = static_cast<std::size_t>(area.size) * static_cast<std::size_t>(area.size);
    Image image{area.size, area.size, 3, std::vector<std::uint8_t>(pixels * 3, 0)};
    std::vector<CellId> nearest(pixels);
    TypeShares shares{};
    std::size_t cavePixels = 0;
    for (int row = 0; row < area.size; ++row) {
        for (int column = 0; column < area.size; ++column) {
            const Point at = pixelPoint(area, column, row);
            const DistrictWeights weights = cache.districtWeights(at.x, at.z);
            const float height = heights.at(column, row);
            const float light =
                hillshade(heights, column, row) * groundLight(height, deepOceanDepth);
            const Color color = districtMix(weights);
            const auto i = heights.index(column, row);
            for (std::size_t c = 0; c < 3; ++c) {
                image.pixels[i * 3 + c] = toByte(color[c] * light);
            }
            nearest[i] = {weights.districts[0].cellX, weights.districts[0].cellZ};
            if (cavesPossible(height, deepOceanDepth)) {
                addShares(shares, weights);
                ++cavePixels;
            }
        }
    }
    darkenBorders(image, nearest);
    const std::size_t sites = drawSites(image, cache, *field.districts(), area);
    if (stats != nullptr) {
        fillMapStats(*stats, shares, cavePixels, pixels, sites);
    }
    return image;
}

Image renderCaveRegionsPreview(const CaveRegion& region, CaveRegionsPreviewStats* stats) {
    requireDistricts(region.field());
    AquiferCache cache(region.field(), region.ground());
    TypeShares sums{};
    std::vector<Color> mixes;
    const auto tint = [&](Image& image) {
        tintDistricts(image, region.eroded(), cache, sums, mixes);
    };
    // The caves take the color of their districts, darker the deeper they are.
    const auto caveTint = [&mixes](std::size_t sample, float depth) {
        const float light = 1.0f - kCaveRegionsPreviewDeepShade * depth;
        std::array<std::uint8_t, 3> rgb{};
        for (std::size_t c = 0; c < rgb.size(); ++c) {
            rgb[c] = toByte(mixes[sample][c] * light);
        }
        return rgb;
    };
    Image image = cave_png::renderCaveMap(region, tint, caveTint, false,
                                          stats != nullptr ? &stats->caves : nullptr);
    if (stats != nullptr) {
        const int size = region.eroded().region().size;
        for (std::size_t t = 0; t < sums.size(); ++t) {
            stats->typeShares[t] = sums[t] / (static_cast<double>(size) * size);
        }
    }
    return image;
}

CaveRegionsMeeting findCaveRegionsMeeting(const CaveField& field, const CaveGround& ground,
                                          double nearX, double nearZ,
                                          std::span<const CaveDistrictType> preferred) {
    requireDistricts(field);
    AquiferCache cache(field, ground);
    const CellId home = cellAt(*field.districts(), nearX, nearZ);
    NearestMeeting any{{nearX, nearZ, false}};
    NearestMeeting wanted{{nearX, nearZ, false}};
    for (int cellZ = home.z - kMeetingReach; cellZ <= home.z + kMeetingReach; ++cellZ) {
        for (int cellX = home.x - kMeetingReach; cellX <= home.x + kMeetingReach; ++cellX) {
            for (const Step step : kNeighborSteps) {
                const CaveDistrict a = cache.district(cellX, cellZ);
                const CaveDistrict b = cache.district(cellX + step.x, cellZ + step.z);
                Point midpoint{};
                if (!isMeeting(cache, ground, a, b, midpoint)) {
                    continue;
                }
                const double distance = dmath::hypot(midpoint.x - nearX, midpoint.z - nearZ);
                any.offer(midpoint, distance);
                if (holdsPreferred(preferred, a, b)) {
                    wanted.offer(midpoint, distance);
                }
            }
        }
    }
    return wanted.meeting.found ? wanted.meeting : any.meeting;
}

bool exportCaveRegionsPngs(const CaveRegionsExport& request,
                           const std::filesystem::path& directory, CaveRegionsStats* stats) {
    requireArea(request.map);
    if (request.previewSize < 2 || !(request.previewMetersPerPixel > 0.0) ||
        !std::isfinite(request.previewMetersPerPixel)) {
        throw std::invalid_argument("cave regions png: empty preview");
    }
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return false;
    }
    CaveRegionsStats local;
    const CaveParams caves = districtCaveParams();
    const TerrainTiles world(request.seed, {}, {}, caves);
    const Image map = renderCaveRegionsMap(*world.caves(), world.caveGround(), request.map,
                                           &local.map);

    const CaveRegionsMeeting meeting =
        findCaveRegionsMeeting(*world.caves(), world.caveGround(), request.map.centerX,
                               request.map.centerZ, request.preferredTypes);
    local.meetingFound = meeting.found;
    local.previewArea = {meeting.x, meeting.z, request.previewMetersPerPixel, request.previewSize};
    CaveRegionParams params;
    params.caves = caves;
    const CaveRegion region(request.seed, local.previewArea, params);
    const Image preview = renderCaveRegionsPreview(region, &local.preview);

    const bool mapWritten = writePng(directory / "cave_regions.png", map);
    const bool previewWritten = writePng(directory / "cave_regions_preview.png", preview);
    if (stats != nullptr) {
        *stats = local;
    }
    return mapWritten && previewWritten;
}

} // namespace terrain

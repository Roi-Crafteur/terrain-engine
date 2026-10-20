#include "caves_png_layers.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <utility>
#include <vector>

namespace terrain {

namespace cave_png {

double sliceU(const CaveSlice& slice, double column) {
    const double center = slice.alongX ? slice.centerX : slice.centerZ;
    return center + (column + 0.5 - slice.width / 2.0) * slice.metersPerPixel;
}

LinePoint slicePoint(const CaveSlice& slice, double u) {
    return slice.alongX ? LinePoint{u, slice.centerZ} : LinePoint{slice.centerX, u};
}

void setPixel(Image& image, int column, int row, Rgb rgb) {
    if (column < 0 || row < 0 || column >= image.width || row >= image.height) {
        return;
    }
    const auto offset = static_cast<std::size_t>((row * image.width + column) * 3);
    std::copy(rgb.begin(), rgb.end(), image.pixels.begin() + static_cast<std::ptrdiff_t>(offset));
}

} // namespace cave_png

namespace {

using cave_png::kSliceMargin;
using cave_png::LinePoint;
using cave_png::Rgb;
using cave_png::setPixel;
using cave_png::slicePoint;
using cave_png::sliceU;

// Rock at the bottom of a section has this share less of the brightness it has at the ground.
constexpr float kRockDarkening = 0.6f;
// The box of the sampler of a section holds the grounds of its columns widened by this much
// (meters) on every side, for the grounds of the speleothems between its columns.
constexpr double kBoxMargin = 8.0;

std::array<std::uint8_t, 3> toArray(const std::uint8_t (&rgb)[3]) {
    return {rgb[0], rgb[1], rgb[2]};
}

// What a pixel of a section holds, for its walls and its speleothems.
enum class Cell : std::uint8_t {
    Open,  // sky, sea or the line of the ground
    Rock,  // under the line of the ground, walls included
    Air,   // carved, dry
    Water, // carved, under the water of a lake (or its ice)
};

// A column of a section: the state of the caves in it, its environment and what its ground is.
struct SliceColumn {
    CaveColumn caves;
    CaveEnvironment environment;
    bool river = false;
};

// A section being drawn: its image, what each pixel holds and the state of each column.
struct Section {
    explicit Section(const CaveSlice& source)
        : slice(source),
          image{source.width, source.height, 3,
                std::vector<std::uint8_t>(pixelCount() * 3, 0)},
          cells(pixelCount(), Cell::Open) {}

    [[nodiscard]] std::size_t pixelCount() const {
        return static_cast<std::size_t>(slice.width) * static_cast<std::size_t>(slice.height);
    }

    // Open outside the image.
    [[nodiscard]] Cell cell(int column, int row) const {
        if (column < 0 || row < 0 || column >= slice.width || row >= slice.height) {
            return Cell::Open;
        }
        return cells[static_cast<std::size_t>(row * slice.width + column)];
    }

    void put(int column, int row, Cell kind, Rgb rgb) {
        cells[static_cast<std::size_t>(row * slice.width + column)] = kind;
        setPixel(image, column, row, rgb);
    }

    // World height of the center of a row.
    [[nodiscard]] double rowY(int row) const {
        return slice.top - (row + 0.5) * slice.metersPerPixel;
    }

    const CaveSlice& slice;
    Image image;
    std::vector<Cell> cells;
    std::vector<SliceColumn> columns;
    std::size_t waterPixels = 0;
};

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

// Color of the rock at a depth below the ground: gray, darker with the depth.
std::array<std::uint8_t, 3> rockRgb(double depth, const CaveParams& params) {
    const double range = params.maxDepth + 2.0 * kSliceMargin;
    const auto fade = static_cast<float>(std::clamp(depth / range, 0.0, 1.0));
    const float light = 1.0f - kRockDarkening * fade;
    std::array<std::uint8_t, 3> rgb{};
    for (std::size_t c = 0; c < 3; ++c) {
        rgb[c] = static_cast<std::uint8_t>(std::lround(kCaveRockRgb[c] * light));
    }
    return rgb;
}

// Sky, sea, line of the ground, rock, and the carved air of a column: the water (or the ice) of
// the lake of its aquifer where its density says so, else the color of its component.
void paintColumn(Section& section, CaveSampler& sampler, int pixelColumn) {
    const CaveSlice& slice = section.slice;
    const SliceColumn& column = section.columns[static_cast<std::size_t>(pixelColumn)];
    const CaveParams& params = sampler.field().params();
    const double ground = column.caves.ground;
    const bool wet = column.caves.waterLevel > kDryLevel;
    for (int row = 0; row < slice.height; ++row) {
        const double y = section.rowY(row);
        if (y >= ground) {
            section.put(pixelColumn, row, Cell::Open, y < kSeaLevel ? kCaveSeaRgb : kCaveSkyRgb);
            continue;
        }
        const auto terrain = static_cast<float>(y - ground);
        const float density = sampler.density(column.caves, y, terrain);
        if (density < 0.0f && ground - y <= slice.metersPerPixel) {
            section.put(pixelColumn, row, Cell::Open,
                        column.river ? kCaveRiverRgb : kCaveGroundRgb);
        } else if (density < 0.0f) {
            section.put(pixelColumn, row, Cell::Rock, rockRgb(ground - y, params));
        } else if (wet && sampler.waterDensity(column.caves, y, terrain, density) < 0.0f) {
            section.put(pixelColumn, row, Cell::Water,
                        column.caves.frozen ? kCaveIceRgb : kCaveWaterRgb);
            ++section.waterPixels;
        } else {
            section.put(pixelColumn, row, Cell::Air, airColor(sampler.voids(column.caves, y)));
        }
    }
}

// Air (dry or under water) around a rock pixel, within the reach of the walls: how many pixels,
// and the sum of their offsets (x to the right, y up), which points toward the air.
struct AirAround {
    int count = 0;
    int x = 0;
    int y = 0;
};

AirAround airAround(const Section& section, int column, int row) {
    constexpr int reach = kCaveWallReach;
    AirAround around;
    for (int dy = -reach; dy <= reach; ++dy) {
        for (int dx = -reach; dx <= reach; ++dx) {
            if ((dx == 0 && dy == 0) || dx * dx + dy * dy > reach * reach) {
                continue;
            }
            const Cell cell = section.cell(column + dx, row + dy);
            if (cell == Cell::Air || cell == Cell::Water) {
                ++around.count;
                around.x += dx;
                around.y -= dy;
            }
        }
    }
    return around;
}

// The wall at a rock pixel at height y of a column, whose normal points toward the air around.
CaveWallSite wallSite(const SliceColumn& column, double y, const AirAround& around) {
    CaveWallSite site;
    site.environment = column.environment;
    site.depth = static_cast<float>(static_cast<double>(column.caves.ground) - y);
    const double length = std::hypot(around.x, around.y);
    site.normalY = length > 0.0 ? static_cast<float>(around.y / length) : 0.0f;
    if (column.caves.waterLevel > kDryLevel) {
        site.aboveWater = static_cast<float>(y - static_cast<double>(column.caves.waterLevel));
        site.frozenWater = column.caves.frozen;
    }
    return site;
}

// The rock within the reach of the air has the color of its material.
void paintWalls(Section& section, const CaveMaterialParams& params) {
    for (int row = 0; row < section.slice.height; ++row) {
        for (int column = 0; column < section.slice.width; ++column) {
            if (section.cell(column, row) != Cell::Rock) {
                continue;
            }
            const AirAround around = airAround(section, column, row);
            if (around.count > 0) {
                const SliceColumn& state = section.columns[static_cast<std::size_t>(column)];
                const CaveWallSite site = wallSite(state, section.rowY(row), around);
                const std::array<std::uint8_t, 3> rgb = caveMaterialRgb(caveMaterial(site, params));
                setPixel(section.image, column, row, rgb);
            }
        }
    }
}

// Plane of a section widened by the reach of the speleothems on both sides of its line.
struct Rect {
    double minX;
    double minZ;
    double maxX;
    double maxZ;
};

Rect sliceRect(const CaveSlice& slice) {
    const double half = slice.width * slice.metersPerPixel / 2.0;
    const double reach = kCaveSpeleothemReach;
    return slice.alongX ? Rect{slice.centerX - half, slice.centerZ - reach, slice.centerX + half,
                               slice.centerZ + reach}
                        : Rect{slice.centerX - reach, slice.centerZ - half,
                               slice.centerX + reach, slice.centerZ + half};
}

// Box of the sampler of a section: its rectangle (a meter more on each side), from under the
// deepest cave of the grounds along it and the lines of the reach of the speleothems at each
// side (cheese caves and speleothems read down to maxDepth below the ground) to over them, and
// over the rows of the section.
CaveBox sectionBox(const CaveRegion& region, const CaveSlice& slice) {
    double lowest = std::numeric_limits<double>::max();
    double highest = std::numeric_limits<double>::lowest();
    for (int column = 0; column < slice.width; ++column) {
        const LinePoint point = slicePoint(slice, sliceU(slice, column));
        for (const double offset : {-kCaveSpeleothemReach, 0.0, kCaveSpeleothemReach}) {
            const double ground = region.groundAt(point.x + (slice.alongX ? 0.0 : offset),
                                                  point.z + (slice.alongX ? offset : 0.0));
            lowest = std::min(lowest, ground);
            highest = std::max(highest, ground);
        }
    }
    const Rect rect = sliceRect(slice);
    const double bottom = std::min(slice.top - slice.height * slice.metersPerPixel,
                                   lowest - region.field().params().maxDepth - kBoxMargin);
    return {rect.minX - 1.0, bottom, rect.minZ - 1.0, rect.maxX + 1.0,
            std::max(slice.top, highest + kBoxMargin), rect.maxZ + 1.0};
}

// Triangle of a speleothem: from its base (width 2 * radius, at least one pixel) to its tip,
// over the air of the pixels it covers.
void drawSpeleothem(Section& section, const Speleothem& speleothem) {
    const CaveSlice& slice = section.slice;
    const double pixel = slice.metersPerPixel;
    const double u = slice.alongX ? speleothem.x : speleothem.z;
    const double length = speleothem.length;
    const double tip = speleothem.kind == SpeleothemKind::Stalactite ? speleothem.y - length
                                                                     : speleothem.y + length;
    const double low = std::min<double>(speleothem.y, tip) - pixel / 2.0;
    const double high = std::max<double>(speleothem.y, tip) + pixel / 2.0;
    const int firstRow = std::max(static_cast<int>(std::ceil((slice.top - high) / pixel - 0.5)), 0);
    const int lastRow = std::min(static_cast<int>(std::floor((slice.top - low) / pixel - 0.5)),
                                 slice.height - 1);
    const double middle = (u - sliceU(slice, 0)) / pixel; // the column of u, fractional
    const double reach = speleothem.radius / pixel + 1.0;
    const int firstColumn = std::max(static_cast<int>(std::floor(middle - reach)), 0);
    const int lastColumn = std::min(static_cast<int>(std::ceil(middle + reach)), slice.width - 1);
    const Rgb color = speleothem.ice ? kCaveSpeleothemIceRgb : kCaveSpeleothemRgb;
    for (int row = firstRow; row <= lastRow; ++row) {
        const double t = std::clamp(std::abs(section.rowY(row) - speleothem.y) / length, 0.0, 1.0);
        const double half = std::max(speleothem.radius * (1.0 - t), pixel / 2.0);
        for (int column = firstColumn; column <= lastColumn; ++column) {
            if (std::abs(sliceU(slice, column) - u) <= half &&
                section.cell(column, row) == Cell::Air) {
                section.put(column, row, Cell::Air, color);
            }
        }
    }
}

// Draws the speleothems of the plane of the section that lie along its line: returns how many.
std::size_t drawSpeleothems(Section& section, const CaveRegion& region, CaveSampler& sampler) {
    const CaveSlice& slice = section.slice;
    const Rect rect = sliceRect(slice);
    const double center = slice.alongX ? slice.centerX : slice.centerZ;
    const double half = slice.width * slice.metersPerPixel / 2.0;
    std::size_t drawn = 0;
    for (const Speleothem& speleothem :
         region.speleothems().place(sampler, rect.minX, rect.minZ, rect.maxX, rect.maxZ)) {
        const double u = slice.alongX ? speleothem.x : speleothem.z;
        if (std::abs(u - center) <= half) {
            drawSpeleothem(section, speleothem);
            ++drawn;
        }
    }
    return drawn;
}

} // namespace

std::array<std::uint8_t, 3> caveMaterialRgb(CaveMaterial material) {
    switch (material) {
    case CaveMaterial::Surface:
        return toArray(kCaveGroundRgb);
    case CaveMaterial::Stone:
        return {105, 112, 135};
    case CaveMaterial::Deepstone:
        return {60, 58, 85};
    case CaveMaterial::Limestone:
        return {225, 215, 170};
    case CaveMaterial::Sandstone:
        return {215, 165, 95};
    case CaveMaterial::Ice:
        return {150, 215, 240};
    case CaveMaterial::Moss:
        return {90, 130, 40};
    case CaveMaterial::Mud:
        return {105, 70, 40};
    case CaveMaterial::Gravel:
        return {160, 148, 120};
    case CaveMaterial::Water:
        return toArray(kCaveWaterRgb);
    case CaveMaterial::Basalt:
        return {45, 40, 48};
    case CaveMaterial::Magma:
        return {150, 35, 15};
    }
    return toArray(kCaveRockRgb);
}

Image renderCaveSlice(const CaveRegion& region, const CaveSlice& slice, CaveSliceStats* stats) {
    Section section(slice);
    CaveSampler sampler = region.sampler(sectionBox(region, slice));
    CaveEnvironmentCache environments(region.environment());
    const float riverReach = static_cast<float>(region.eroded().region().metersPerPixel) / 2.0f;
    section.columns.reserve(static_cast<std::size_t>(slice.width));
    for (int column = 0; column < slice.width; ++column) {
        const LinePoint point = slicePoint(slice, sliceU(slice, column));
        SliceColumn state;
        state.caves = sampler.column(point.x, point.z, region.groundAt(point.x, point.z));
        state.environment = environments.at(point.x, point.z);
        state.river = region.riverDistanceAt(point.x, point.z) < riverReach;
        section.columns.push_back(state);
        paintColumn(section, sampler, column);
    }
    paintWalls(section, region.field().params().materials);
    const std::size_t speleothems = drawSpeleothems(section, region, sampler);
    if (stats != nullptr) {
        stats->speleothems = speleothems;
        stats->waterFraction =
            static_cast<double>(section.waterPixels) / static_cast<double>(section.pixelCount());
    }
    return std::move(section.image);
}

} // namespace terrain

#include "terrain/generation/cliff_region.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace terrain {

namespace {

// A river cell higher than a cirque floor by more than this would be cut by the cirque.
constexpr float kFloorTolerance = 1.0e-3f;

// Cell coordinates this close to an integer are snapped to it: the samples of the grids
// then get exact weights despite the rounding of the world-to-cell conversion.
constexpr double kCellSnap = 1.0e-9;

// A position in cell units of the grids (cell centers at integers), as in
// ErodedRegion::heightAt.
struct CellPoint {
    double column;
    double row;
};

double snapToCell(double value) {
    const double nearest = std::round(value);
    return std::abs(value - nearest) < kCellSnap ? nearest : value;
}

CellPoint cellPoint(const HeightmapRegion& region, double x, double z) {
    const double half = region.size / 2.0;
    return {snapToCell((x - region.centerX) / region.metersPerPixel + half - 0.5),
            snapToCell((z - region.centerZ) / region.metersPerPixel + half - 0.5)};
}

// Bilinear interpolation of a per-cell grid, the cell index clamped like in
// ErodedRegion::heightAt.
float interpolate(const std::vector<float>& values, int size, const CellPoint& point) {
    const int i = std::clamp(static_cast<int>(point.column), 0, size - 2);
    const int j = std::clamp(static_cast<int>(point.row), 0, size - 2);
    const double u = point.column - i;
    const double v = point.row - j;
    const auto at = [&](int column, int row) {
        return static_cast<double>(values[static_cast<std::size_t>(row * size + column)]);
    };
    return static_cast<float>(at(i, j) * (1 - u) * (1 - v) + at(i + 1, j) * u * (1 - v) +
                              at(i, j + 1) * (1 - u) * v + at(i + 1, j + 1) * u * v);
}

// Slope |grad H| of the eroded relief by central differences over one cell.
float reliefSlope(const ErodedRegion& eroded, const CliffParams& params, double x, double z) {
    const double step = 0.5 * eroded.region().metersPerPixel;
    const auto height = [&](double hx, double hz) {
        return static_cast<double>(eroded.heightAt(hx, hz));
    };
    const double gradX = (height(x + step, z) - height(x - step, z)) / (2.0 * step);
    const double gradZ = (height(x, z + step) - height(x, z - step)) / (2.0 * step);
    return std::clamp(static_cast<float>(dmath::hypot(gradX, gradZ)), params.minSlope,
                      params.maxSlope);
}

// Half-width of the gorge of a river draining catchment m^2: none below the minimum
// catchment, growing by a smooth ramp over gorgeWidthDoublings doublings of it.
float gorgeWidth(double catchment, const CliffParams& params) {
    if (catchment < params.gorgeMinCatchment) {
        return 0.0f;
    }
    const auto doublings = static_cast<float>(dmath::log2(catchment / params.gorgeMinCatchment));
    return params.gorgeMaxHalfWidth *
           noise::smoothstep(0.0f, static_cast<float>(params.gorgeWidthDoublings), doublings);
}

// For every cell: the nearest river cell within reach, its water height and its gorge.
struct RiverProximity {
    std::vector<float> distance;
    std::vector<float> level;
    std::vector<float> halfWidth;
};

struct RiverSource {
    int column;
    int row;
    float level;
    float halfWidth;
};

// Offers a river cell to the cells within radius of it. The comparison is strict, so a tie
// goes to the river cell that was visited first, the lowest index.
void spreadRiver(RiverProximity& out, const HeightGrid& grid, const RiverSource& source,
                 double radius) {
    const double mpp = grid.metersPerPixel;
    const int reach = static_cast<int>(std::ceil(radius / mpp));
    for (int dr = -reach; dr <= reach; ++dr) {
        for (int dc = -reach; dc <= reach; ++dc) {
            const int column = source.column + dc;
            const int row = source.row + dr;
            const double meters =
                dmath::hypot(static_cast<double>(dc), static_cast<double>(dr)) * mpp;
            if (column < 0 || row < 0 || column >= grid.size || row >= grid.size ||
                meters > radius) {
                continue;
            }
            const std::size_t cell = grid.index(column, row);
            const auto distance = static_cast<float>(meters);
            if (distance < out.distance[cell]) {
                out.distance[cell] = distance;
                out.level[cell] = source.level;
                out.halfWidth[cell] = source.halfWidth;
            }
        }
    }
}

// Reaches two cells past riverReach, so that every column within riverReach of a river is
// surrounded by cells that know it.
RiverProximity computeRiverProximity(const ErodedRegion& eroded, const CliffParams& params) {
    const HeightGrid& grid = eroded.grid();
    const RiverNetwork& rivers = eroded.rivers();
    const std::size_t count = grid.heights.size();
    RiverProximity out{std::vector<float>(count, kNoRiverDistance), grid.heights,
                       std::vector<float>(count, 0.0f)};
    const double radius = params.riverReach + 2.0 * grid.metersPerPixel;
    for (int row = 0; row < grid.size; ++row) {
        for (int column = 0; column < grid.size; ++column) {
            const int index = row * grid.size + column;
            if (rivers.isRiver(index)) {
                const auto cell = static_cast<std::size_t>(index);
                const RiverSource source{column, row, grid.heights[cell],
                                         gorgeWidth(rivers.catchment[cell], params)};
                spreadRiver(out, grid, source, radius);
            }
        }
    }
    return out;
}

// True when no river cell strictly inside the cylinder is higher than its floor: carving
// the cirque would then cut another, higher river.
bool cylinderIsClear(const ErodedRegion& eroded, double axisX, double axisZ, float floor,
                     float radius) {
    const HeightGrid& grid = eroded.grid();
    const CellPoint center = cellPoint(eroded.region(), axisX, axisZ);
    const int reach = static_cast<int>(std::ceil(radius / grid.metersPerPixel)) + 1;
    const int centerColumn = static_cast<int>(std::floor(center.column));
    const int centerRow = static_cast<int>(std::floor(center.row));
    const int firstColumn = std::max(0, centerColumn - reach);
    const int lastColumn = std::min(grid.size - 1, centerColumn + reach);
    const int firstRow = std::max(0, centerRow - reach);
    const int lastRow = std::min(grid.size - 1, centerRow + reach);
    for (int row = firstRow; row <= lastRow; ++row) {
        for (int column = firstColumn; column <= lastColumn; ++column) {
            const double distance =
                dmath::hypot(eroded.sampleX(column) - axisX, eroded.sampleZ(row) - axisZ);
            if (distance < radius && eroded.rivers().isRiver(row * grid.size + column) &&
                grid.at(column, row) > floor + kFloorTolerance) {
                return false;
            }
        }
    }
    return true;
}

// Columns within this distance of a cirque axis carve it; farther, it could only change the
// density deep inside the rock.
double cirqueReach(const CliffParams& params) {
    return static_cast<double>(params.cirqueRadius) + 2.0 * params.densityClamp;
}

// True when the cliff mask is positive at the axis and on two rings around it, out to radius.
// The cirques are carved at full strength: a column where the mask is 0 must keep exactly the
// eroded relief, so a cirque must not reach one.
bool insideCliffs(const CliffRegion& region, double axisX, double axisZ, double radius) {
    constexpr int kRingPoints = 16;
    const auto maskAt = [&](double x, double z) {
        return region.field().maskAt(x, z, region.eroded().heightAt(x, z),
                                     region.borderWeightAt(x, z));
    };
    bool inside = maskAt(axisX, axisZ) > 0.0f;
    for (int i = 0; i < kRingPoints && inside; ++i) {
        const double turns = static_cast<double>(i) / kRingPoints;
        for (const double r : {0.5 * radius, radius}) {
            const float mask =
                maskAt(axisX + r * dmath::cosTurns(turns), axisZ + r * dmath::sinTurns(turns));
            inside = inside && mask > 0.0f;
        }
    }
    return inside;
}

// Cirque just below cascade k of the region, if the cascade is inland, inside the cliffs (its
// whole reach included) and the cirque would not cut another river. The axis is pushed past
// the receiver so that the cascade cell stays on the wall.
bool makeCirque(const CliffRegion& region, int k, Cirque& cirque) {
    const ErodedRegion& eroded = region.eroded();
    const HeightGrid& grid = eroded.grid();
    const Cascade& cascade = eroded.rivers().cascades[static_cast<std::size_t>(k)];
    const int cell = cascade.row * grid.size + cascade.column;
    const int receiver = eroded.rivers().receiverOf(cell);
    if (receiver == kNoReceiver || grid.heights[static_cast<std::size_t>(receiver)] < kSeaLevel) {
        return false;
    }
    const CliffParams& params = region.field().params();
    const double x = eroded.sampleX(cascade.column);
    const double z = eroded.sampleZ(cascade.row);
    const float mask = region.field().maskAt(x, z, grid.at(cascade.column, cascade.row),
                                             region.borderWeightAt(x, z));
    if (mask < params.cirqueMinMask) {
        return false;
    }
    const double toX = eroded.sampleX(receiver % grid.size) - x;
    const double toZ = eroded.sampleZ(receiver / grid.size) - z;
    const double scale =
        (params.cirqueRadius + 0.25 * grid.metersPerPixel) / dmath::hypot(toX, toZ);
    const double axisX = x + toX * scale;
    const double axisZ = z + toZ * scale;
    const float floor = grid.heights[static_cast<std::size_t>(receiver)];
    if (!cylinderIsClear(eroded, axisX, axisZ, floor, params.cirqueRadius) ||
        !insideCliffs(region, axisX, axisZ, cirqueReach(params))) {
        return false;
    }
    cirque = {axisX, axisZ, floor, k};
    return true;
}

std::vector<Cirque> findCirques(const CliffRegion& region) {
    std::vector<Cirque> cirques;
    const int count = static_cast<int>(region.eroded().rivers().cascades.size());
    for (int k = 0; k < count; ++k) {
        Cirque cirque;
        if (makeCirque(region, k, cirque)) {
            cirques.push_back(cirque);
        }
    }
    return cirques;
}

// The cirques within reach of the column, in index order, at most kMaxColumnCirques.
void addCirques(CliffColumn& column, const std::vector<Cirque>& cirques,
                const CliffParams& params, double x, double z) {
    const double reach = cirqueReach(params);
    for (const Cirque& cirque : cirques) {
        const double distance = dmath::hypot(x - cirque.x, z - cirque.z);
        if (distance < reach && column.cirqueCount < kMaxColumnCirques) {
            column.cirques[static_cast<std::size_t>(column.cirqueCount)] = {
                static_cast<float>(distance), cirque.floor};
            ++column.cirqueCount;
        }
    }
}

// Mask and surface at every sample of the eroded grid.
void computeGrids(const CliffRegion& region, std::vector<float>& mask, HeightGrid& surface) {
    mask.assign(surface.heights.size(), 0.0f);
    for (int row = 0; row < surface.size; ++row) {
        for (int column = 0; column < surface.size; ++column) {
            const CliffColumn at =
                region.columnAt(region.eroded().sampleX(column), region.eroded().sampleZ(row));
            const std::size_t index = surface.index(column, row);
            mask[index] = at.cliff;
            surface.heights[index] = region.field().topSurface(at);
        }
    }
}

} // namespace

CliffRegion::CliffRegion(std::uint64_t seed, const HeightmapRegion& region,
                         const CliffRegionParams& params, const WorldSize& world)
    : eroded_(seed, region, params.eroded, world), field_(seed, params.cliffs, world),
      surface_(eroded_.grid()) {
    RiverProximity proximity = computeRiverProximity(eroded_, params.cliffs);
    riverDistance_ = std::move(proximity.distance);
    riverLevel_ = std::move(proximity.level);
    gorgeHalfWidth_ = std::move(proximity.halfWidth);
    cirques_ = findCirques(*this);
    computeGrids(*this, mask_, surface_);
}

float CliffRegion::borderWeightAt(double x, double z) const {
    const HeightmapRegion& region = eroded_.region();
    const CellPoint point = cellPoint(region, eroded_.imageX(x), eroded_.imageZ(z));
    const double last = region.size - 1;
    const double distance = std::min({point.column, point.row, last - point.column,
                                      last - point.row});
    if (!(distance > 0.0)) {
        return 0.0f;
    }
    const auto margin = static_cast<float>(field_.params().borderMargin);
    return margin > 0.0f ? noise::smoothstep(0.0f, margin, static_cast<float>(distance)) : 1.0f;
}

CliffColumn CliffRegion::columnAt(double worldX, double worldZ) const {
    const double x = eroded_.imageX(worldX);
    const double z = eroded_.imageZ(worldZ);
    CliffColumn column;
    column.ground = eroded_.heightAt(x, z);
    const float weight = borderWeightAt(x, z);
    if (weight <= 0.0f) {
        return column;
    }
    column.cliff = field_.maskAt(x, z, column.ground, weight);
    if (column.cliff <= 0.0f) {
        return column;
    }
    const CliffParams& params = field_.params();
    const int size = eroded_.region().size;
    const CellPoint point = cellPoint(eroded_.region(), x, z);
    column.slope = reliefSlope(eroded_, params, x, z);
    column.rimRecession = field_.rimRecessionAt(x, z);
    column.riverDistance = interpolate(riverDistance_, size, point);
    column.riverLevel = interpolate(riverLevel_, size, point);
    column.gorgeHalfWidth = interpolate(gorgeHalfWidth_, size, point);
    addCirques(column, cirques_, params, x, z);
    return column;
}

float CliffRegion::densityAt(double x, double y, double z) const {
    return field_.density(columnAt(x, z), y);
}

float CliffRegion::surfaceAt(double x, double z) const {
    return field_.topSurface(columnAt(x, z));
}

CliffChunkColumns sampleCliffColumns(const CliffRegion& region, double originX, double originZ,
                                     double spacing) {
    CliffChunkColumns result;
    result.spacing = spacing;
    result.columns.reserve(static_cast<std::size_t>(kGridSamples * kGridSamples));
    result.minHeight = std::numeric_limits<float>::max();
    result.maxHeight = std::numeric_limits<float>::lowest();
    for (int z = 0; z < kGridSamples; ++z) {
        for (int x = 0; x < kGridSamples; ++x) {
            result.columns.push_back(region.columnAt(originX + x * spacing, originZ + z * spacing));
            const float top = region.field().topSurface(result.columns.back());
            result.minHeight = std::min(result.minHeight, top);
            result.maxHeight = std::max(result.maxHeight, top);
        }
    }
    return result;
}

DensityGrid cliffDensityFromColumns(const CliffField& field, const CliffChunkColumns& columns,
                                    double originY) {
    DensityGrid grid(0.0f);
    for (int z = 0; z < kGridSamples; ++z) {
        for (int y = 0; y < kGridSamples; ++y) {
            const double sampleY = originY + y * columns.spacing;
            for (int x = 0; x < kGridSamples; ++x) {
                grid.set(x, y, z, field.density(columns.at(x, z), sampleY));
            }
        }
    }
    return grid;
}

} // namespace terrain

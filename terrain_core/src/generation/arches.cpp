#include "terrain/generation/arches.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/sdf.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace terrain {

namespace {

// Stream of the arch candidates, distinct from the other layers.
constexpr std::uint64_t kArchStream = 0x41524348; // "ARCH"

// The half-torus reaches this far below the springline, into the pillars, and the space
// between the pillars this far above it, into the half-disk: the parts overlap, so no sample
// falls on a seam between them.
constexpr double kJointOverlap = 1.0;

// Draws of a candidate, one stream index each.
enum Draw : std::uint64_t { kChanceDraw, kOffsetXDraw, kOffsetZDraw, kAngleDraw, kSpanDraw };

std::uint64_t cellSeed(std::uint64_t seed, int cellX, int cellZ) {
    const auto x = static_cast<std::uint64_t>(static_cast<std::int64_t>(cellX));
    const auto z = static_cast<std::uint64_t>(static_cast<std::int64_t>(cellZ));
    return noise::deriveSeed(noise::deriveSeed(seed, x), z);
}

// Uniform number in [0, 1), 2^24 steps.
double unitDraw(std::uint64_t cell, Draw draw) {
    return static_cast<double>(noise::deriveSeed(cell, draw) >> 40) / 16777216.0;
}

// Position in the frame of an arch: along the span and across it, from the center.
struct Local {
    double along;
    double across;
};

Local localOf(const Arch& arch, double x, double z) {
    const double dx = x - arch.x;
    const double dz = z - arch.z;
    return {dx * arch.directionX + dz * arch.directionZ, dz * arch.directionX - dx * arch.directionZ};
}

// Square where the reach of an arch must lie: the samples of the region, without the ring
// where the cliff mask fades in from the border.
struct Bounds {
    double minX;
    double maxX;
    double minZ;
    double maxZ;
};

Bounds placementBounds(const CliffRegion& cliffs) {
    const ErodedRegion& eroded = cliffs.eroded();
    const int margin = cliffs.field().params().borderMargin;
    const int last = eroded.region().size - 1 - margin;
    return {eroded.sampleX(margin), eroded.sampleX(last), eroded.sampleZ(margin),
            eroded.sampleZ(last)};
}

bool reachInside(const Bounds& bounds, const Arch& arch, double reach) {
    return arch.x - reach >= bounds.minX && arch.x + reach <= bounds.maxX &&
           arch.z - reach >= bounds.minZ && arch.z + reach <= bounds.maxZ;
}

// True when no river cell (cascades included), no ocean cell and no cirque is within reach
// of the arch. The cells are searched two cells farther, like the river proximity grids.
bool clearOfWater(const CliffRegion& cliffs, const Arch& arch, double reach) {
    const ErodedRegion& eroded = cliffs.eroded();
    const HeightGrid& grid = eroded.grid();
    const double radius = reach + 2.0 * grid.metersPerPixel;
    const auto cell = [&](double offset, double origin) {
        return std::clamp(static_cast<int>(std::floor(offset / grid.metersPerPixel + origin)), 0,
                          grid.size - 1);
    };
    const int firstColumn = cell(arch.x - radius - eroded.sampleX(0), 0.0);
    const int lastColumn = cell(arch.x + radius - eroded.sampleX(0), 1.0);
    const int firstRow = cell(arch.z - radius - eroded.sampleZ(0), 0.0);
    const int lastRow = cell(arch.z + radius - eroded.sampleZ(0), 1.0);
    for (int row = firstRow; row <= lastRow; ++row) {
        for (int column = firstColumn; column <= lastColumn; ++column) {
            const int index = row * grid.size + column;
            const double distance =
                dmath::hypot(eroded.sampleX(column) - arch.x, eroded.sampleZ(row) - arch.z);
            if (distance <= radius && (eroded.rivers().isRiver(index) ||
                                       grid.heights[static_cast<std::size_t>(index)] < kSeaLevel)) {
                return false;
            }
        }
    }
    const CliffParams& params = cliffs.field().params();
    const double cirqueReach = params.cirqueRadius + 2.0 * params.densityClamp;
    return std::none_of(cliffs.cirques().begin(), cliffs.cirques().end(), [&](const Cirque& c) {
        return dmath::hypot(c.x - arch.x, c.z - arch.z) < reach + cirqueReach;
    });
}

// Lowest and highest rock surface over the footprint of the arch (pillars and opening),
// sampled every groundStep at most; false as soon as the cliff mask is too low or the
// surface too uneven.
bool levelFootprint(const CliffRegion& cliffs, const ArchField& field, const Arch& arch,
                    float& low, float& high) {
    const ArchParams& params = field.params();
    const double alongHalf = arch.halfSpan + params.pillarRadius;
    const double acrossHalf = std::max(params.pillarRadius, params.openingHalfDepth);
    const int alongSteps = static_cast<int>(std::ceil(2.0 * alongHalf / params.groundStep));
    const int acrossSteps = static_cast<int>(std::ceil(2.0 * acrossHalf / params.groundStep));
    low = std::numeric_limits<float>::max();
    high = std::numeric_limits<float>::lowest();
    for (int j = 0; j <= acrossSteps; ++j) {
        for (int i = 0; i <= alongSteps; ++i) {
            const double along = alongHalf * (2.0 * i / alongSteps - 1.0);
            const double across = acrossHalf * (2.0 * j / acrossSteps - 1.0);
            const double x = arch.x + along * arch.directionX - across * arch.directionZ;
            const double z = arch.z + along * arch.directionZ + across * arch.directionX;
            const CliffColumn column = cliffs.columnAt(x, z);
            if (column.cliff < params.minCliffMask) {
                return false;
            }
            const float surface = cliffs.field().topSurface(column);
            low = std::min(low, surface);
            high = std::max(high, surface);
            if (high - low > params.maxGroundRelief) {
                return false;
            }
        }
    }
    return true;
}

// Checks a candidate against the region and the arches kept so far, cheapest tests first,
// and sets its base.
bool fits(const CliffRegion& cliffs, const ArchField& field, const Bounds& bounds,
          const std::vector<Arch>& kept, Arch& arch) {
    const double reach = field.reach(arch);
    if (!reachInside(bounds, arch, reach) ||
        cliffs.columnAt(arch.x, arch.z).cliff < field.params().minCliffMask) {
        return false;
    }
    for (const Arch& other : kept) {
        if (dmath::hypot(other.x - arch.x, other.z - arch.z) < reach + field.reach(other)) {
            return false;
        }
    }
    float low = 0.0f;
    float high = 0.0f;
    if (!clearOfWater(cliffs, arch, reach) || !levelFootprint(cliffs, field, arch, low, high)) {
        return false;
    }
    arch.base = low;
    return true;
}

} // namespace

ArchField::ArchField(std::uint64_t seed, const ArchParams& params)
    : params_(params), seed_(noise::deriveSeed(seed, kArchStream)) {}

bool ArchField::candidate(int cellX, int cellZ, Arch& arch) const {
    const std::uint64_t cell = cellSeed(seed_, cellX, cellZ);
    if (unitDraw(cell, kChanceDraw) >= static_cast<double>(params_.chance)) {
        return false;
    }
    const double offsetX = 0.5 + params_.jitter * (unitDraw(cell, kOffsetXDraw) - 0.5);
    const double offsetZ = 0.5 + params_.jitter * (unitDraw(cell, kOffsetZDraw) - 0.5);
    const double turns = 0.5 * unitDraw(cell, kAngleDraw); // a span has no side
    const double span = params_.minHalfSpan +
                        (params_.maxHalfSpan - params_.minHalfSpan) * unitDraw(cell, kSpanDraw);
    arch = {(cellX + offsetX) * params_.spacing,
            (cellZ + offsetZ) * params_.spacing,
            dmath::cosTurns(turns),
            dmath::sinTurns(turns),
            static_cast<float>(span),
            0.0f,
            cellX,
            cellZ};
    return true;
}

float ArchField::springline(const Arch& arch) const {
    return arch.base + params_.pillarHeight;
}

float ArchField::crown(const Arch& arch) const {
    return springline(arch) + arch.halfSpan + params_.tubeRadius;
}

float ArchField::openingTop(const Arch& arch) const {
    return springline(arch) + arch.halfSpan - params_.tubeRadius;
}

double ArchField::boundingRadius(const Arch& arch) const {
    const double pillars = static_cast<double>(arch.halfSpan) + params_.pillarRadius;
    const double ring = dmath::hypot(static_cast<double>(arch.halfSpan) + params_.tubeRadius,
                                     std::max(params_.tubeRadius, params_.openingHalfDepth));
    return std::max(pillars, ring);
}

double ArchField::reach(const Arch& arch) const {
    return boundingRadius(arch) + params_.reachMargin;
}

float ArchField::rock(const Arch& arch, double x, double y, double z) const {
    const Local local = localOf(arch, x, z);
    const double top = springline(arch);
    const double rise = y - top;
    // Half-torus in the vertical plane of the span, cut at the springline.
    const auto radial = static_cast<float>(dmath::hypot(local.along, rise));
    const float ring = sdf::intersect(
        sdf::torus(radial, static_cast<float>(local.across), arch.halfSpan, params_.tubeRadius),
        static_cast<float>(-rise - kJointOverlap));
    const double bottom = static_cast<double>(arch.base) - params_.anchorDepth;
    const auto pillar = [&](double side) {
        const auto distance = static_cast<float>(
            dmath::hypot(local.along - side * static_cast<double>(arch.halfSpan), local.across));
        return sdf::verticalCylinder(distance, params_.pillarRadius, y, bottom, top);
    };
    return sdf::unite(ring, sdf::unite(pillar(-1.0), pillar(1.0)));
}

float ArchField::opening(const Arch& arch, double x, double y, double z) const {
    const Local local = localOf(arch, x, z);
    const double rise = y - springline(arch);
    const auto across = static_cast<float>(std::abs(local.across)) - params_.openingHalfDepth;
    // Half-disk inside the half-torus.
    const float vault =
        sdf::intersect(sdf::intersect(static_cast<float>(dmath::hypot(local.along, rise)) -
                                          (arch.halfSpan - params_.tubeRadius),
                                      across),
                       static_cast<float>(-rise));
    // Between the pillars, from the base up to the springline.
    const float door = sdf::intersect(
        sdf::intersect(static_cast<float>(std::abs(local.along)) -
                           (arch.halfSpan - params_.pillarRadius),
                       across),
        sdf::intersect(static_cast<float>(static_cast<double>(arch.base) - y),
                       static_cast<float>(rise - kJointOverlap)));
    return sdf::unite(vault, door);
}

float ArchField::apply(const Arch& arch, float terrain, double x, double y, double z) const {
    return sdf::unite(sdf::subtract(terrain, opening(arch, x, y, z)), rock(arch, x, y, z));
}

std::vector<Arch> placeArches(const CliffRegion& cliffs, const ArchField& field) {
    const Bounds bounds = placementBounds(cliffs);
    const double spacing = field.params().spacing;
    const auto cell = [&](double coordinate) {
        return static_cast<int>(std::floor(coordinate / spacing));
    };
    std::vector<Arch> arches;
    for (int cellZ = cell(bounds.minZ); cellZ <= cell(bounds.maxZ); ++cellZ) {
        for (int cellX = cell(bounds.minX); cellX <= cell(bounds.maxX); ++cellX) {
            Arch arch;
            if (field.candidate(cellX, cellZ, arch) && fits(cliffs, field, bounds, arches, arch)) {
                arches.push_back(arch);
            }
        }
    }
    return arches;
}

} // namespace terrain

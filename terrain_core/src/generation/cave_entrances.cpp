#include "terrain/generation/cave_entrances.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/aquifers.hpp"
#include "terrain/generation/cliffs.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>

namespace terrain {

namespace {

// Draws of a candidate, one stream index each.
enum Draw : std::uint64_t {
    kChanceDraw,
    kOffsetXDraw,
    kOffsetZDraw,
    kAngleDraw,
    kRunDraw,
    kDepthDraw,
    kTubeDraw,
    kChamberDraw,
};

// Fractions of the run where the ground over the tube is probed (the site is probed too).
constexpr double kProbes[] = {0.25, 0.5, 0.75, 1.0};

std::uint64_t cellSeed(std::uint64_t seed, int cellX, int cellZ) {
    const auto x = static_cast<std::uint64_t>(static_cast<std::int64_t>(cellX));
    const auto z = static_cast<std::uint64_t>(static_cast<std::int64_t>(cellZ));
    return noise::deriveSeed(noise::deriveSeed(seed, x), z);
}

// Uniform number in [0, 1), 2^24 steps.
double unitDraw(std::uint64_t cell, Draw draw) {
    return static_cast<double>(noise::deriveSeed(cell, draw) >> 40) / 16777216.0;
}

float drawBetween(std::uint64_t cell, Draw draw, float low, float high) {
    return low + static_cast<float>(unitDraw(cell, draw)) * (high - low);
}

// Parameter in [0, 1] of the point of the segment (ax, az) - (bx, bz) nearest to (x, z).
double nearestOnSegment(double ax, double az, double bx, double bz, double x, double z) {
    const double dx = bx - ax;
    const double dz = bz - az;
    const double length2 = dx * dx + dz * dz;
    if (length2 <= 0.0) {
        return 0.0;
    }
    return std::clamp(((x - ax) * dx + (z - az) * dz) / length2, 0.0, 1.0);
}

double largestRadius(const CaveEntrance& entrance) {
    return std::max(entrance.tubeRadius, entrance.chamberRadius);
}

// The ground along the entrance stands above the shore height, with no river near it.
bool clearAlong(const CaveEntrance& entrance, const CaveGround& ground, const CaveParams& params) {
    const float shore = kSeaLevel + params.entrances.minShoreHeight;
    const auto clear = [&](double x, double z) {
        if (ground.height(x, z) < shore) {
            return false;
        }
        const float river = ground.riverDistance ? ground.riverDistance(x, z) : kNoRiverDistance;
        return river >= params.riverClearance + static_cast<float>(largestRadius(entrance)) +
                            params.fade;
    };
    if (!clear(entrance.topX, entrance.topZ)) {
        return false;
    }
    for (const double t : kProbes) {
        const double x = entrance.topX + t * (entrance.bottomX - entrance.topX);
        const double z = entrance.topZ + t * (entrance.bottomZ - entrance.topZ);
        if (!clear(x, z)) {
            return false;
        }
    }
    return true;
}

// The chamber lies within maxDepth and under at least half the minimum roof.
bool chamberCovered(const CaveEntrance& entrance, const CaveGround& ground,
                    const CaveParams& params) {
    const double depth = ground.height(entrance.bottomX, entrance.bottomZ) - entrance.bottomY;
    return depth + entrance.chamberRadius <= params.maxDepth - 1.0f &&
           depth - entrance.chamberRadius >= params.minRoof / 2.0f;
}

// Every part of the tube and the chamber stands above the water and the barrier tops of the
// aquifers under it, by entranceMargin: the entrance stays dry and is never cut. The axis is
// probed every kAxisStep meters.
constexpr double kAxisStep = 2.0;

bool aboveAquifers(const CaveEntrance& entrance, AquiferCache& aquifers,
                   const CaveParams& params) {
    const double length =
        dmath::hypot(entrance.bottomX - entrance.topX, entrance.bottomY - entrance.topY,
                     entrance.bottomZ - entrance.topZ);
    const int steps = std::max(1, static_cast<int>(std::ceil(length / kAxisStep)));
    for (int step = 0; step <= steps; ++step) {
        const double t = static_cast<double>(step) / steps;
        const double radius = step == steps ? entrance.chamberRadius : entrance.tubeRadius;
        const double x = entrance.topX + t * (entrance.bottomX - entrance.topX);
        const double y = entrance.topY + t * (entrance.bottomY - entrance.topY);
        const double z = entrance.topZ + t * (entrance.bottomZ - entrance.topZ);
        const double level = aquifers.highestLevelNear(x, z, radius);
        if (y - radius < level + params.aquifers.entranceMargin) {
            return false;
        }
    }
    return true;
}

} // namespace

double entranceAxisDistance(const CaveEntrance& entrance, double x, double z) {
    const double t = nearestOnSegment(entrance.topX, entrance.topZ, entrance.bottomX,
                                      entrance.bottomZ, x, z);
    const double px = entrance.topX + t * (entrance.bottomX - entrance.topX);
    const double pz = entrance.topZ + t * (entrance.bottomZ - entrance.topZ);
    return dmath::hypot(x - px, z - pz);
}

bool entranceColumn(const CaveEntrance& entrance, double x, double z, double margin) {
    const double chamber = dmath::hypot(x - entrance.bottomX, z - entrance.bottomZ);
    return entranceAxisDistance(entrance, x, z) <= entrance.tubeRadius + margin ||
           chamber <= entrance.chamberRadius + margin;
}

float entranceVoidAt(const CaveEntrance& entrance, double x, double y, double z) {
    const double dx = entrance.bottomX - entrance.topX;
    const double dy = entrance.bottomY - entrance.topY;
    const double dz = entrance.bottomZ - entrance.topZ;
    const double length2 = dx * dx + dy * dy + dz * dz;
    const double t = std::clamp(
        ((x - entrance.topX) * dx + (y - entrance.topY) * dy + (z - entrance.topZ) * dz) /
            length2,
        0.0, 1.0);
    const double tube = dmath::hypot(x - (entrance.topX + t * dx), y - (entrance.topY + t * dy),
                                     z - (entrance.topZ + t * dz)) -
                        entrance.tubeRadius;
    const double chamber =
        dmath::hypot(x - entrance.bottomX, y - entrance.bottomY, z - entrance.bottomZ) -
        entrance.chamberRadius;
    return static_cast<float>(std::min(tube, chamber));
}

bool CaveField::entrance(int cellX, int cellZ, const CaveGround& ground, AquiferCache& aquifers,
                         CaveEntrance& result) const {
    const EntranceParams& p = params_.entrances;
    const std::uint64_t cell = cellSeed(entranceSeed_, cellX, cellZ);
    if (unitDraw(cell, kChanceDraw) >= static_cast<double>(p.chance)) {
        return false;
    }
    const double offsetX = (unitDraw(cell, kOffsetXDraw) - 0.5) * p.jitter;
    const double offsetZ = (unitDraw(cell, kOffsetZDraw) - 0.5) * p.jitter;
    const double siteX = (cellX + 0.5 + offsetX) * p.spacing;
    const double siteZ = (cellZ + 0.5 + offsetZ) * p.spacing;
    const double turns = unitDraw(cell, kAngleDraw);
    const double run = drawBetween(cell, kRunDraw, p.minRun, p.maxRun);
    const float site = ground.height(siteX, siteZ);

    CaveEntrance entrance;
    entrance.topX = siteX;
    entrance.topY = static_cast<double>(site) + p.aboveGround;
    entrance.topZ = siteZ;
    entrance.bottomX = siteX + run * dmath::cosTurns(turns);
    entrance.bottomY = static_cast<double>(site) - drawBetween(cell, kDepthDraw, p.minDepth,
                                                               p.maxDepth);
    entrance.bottomZ = siteZ + run * dmath::sinTurns(turns);
    entrance.tubeRadius = drawBetween(cell, kTubeDraw, p.minTubeRadius, p.maxTubeRadius);
    entrance.chamberRadius =
        drawBetween(cell, kChamberDraw, p.minChamberRadius, p.maxChamberRadius);
    entrance.cellX = cellX;
    entrance.cellZ = cellZ;
    if (!clearAlong(entrance, ground, params_) || !chamberCovered(entrance, ground, params_)) {
        return false;
    }
    if (!aboveAquifers(entrance, aquifers, params_)) {
        return false;
    }
    result = entrance;
    return true;
}

bool CaveField::entrance(int cellX, int cellZ, const CaveGround& ground,
                         CaveEntrance& result) const {
    AquiferCache aquifers(*this, ground);
    return entrance(cellX, cellZ, ground, aquifers, result);
}

std::vector<CaveEntrance> CaveField::entrancesNear(double minX, double minZ, double maxX,
                                                   double maxZ, const CaveGround& ground) const {
    AquiferCache aquifers(*this, ground);
    return entrancesNear(minX, minZ, maxX, maxZ, ground, aquifers);
}

std::vector<CaveEntrance> CaveField::entrancesNear(double minX, double minZ, double maxX,
                                                   double maxZ, const CaveGround& ground,
                                                   AquiferCache& aquifers) const {
    const EntranceParams& p = params_.entrances;
    const double margin = p.maxRun + std::max(p.maxTubeRadius, p.maxChamberRadius) +
                          kEntranceReachMargin;
    const auto first = [&](double value) {
        return static_cast<int>(std::floor((value - margin) / p.spacing));
    };
    const auto last = [&](double value) {
        return static_cast<int>(std::floor((value + margin) / p.spacing));
    };
    std::vector<CaveEntrance> entrances;
    for (int cellZ = first(minZ); cellZ <= last(maxZ); ++cellZ) {
        for (int cellX = first(minX); cellX <= last(maxX); ++cellX) {
            CaveEntrance entrance;
            if (!this->entrance(cellX, cellZ, ground, aquifers, entrance)) {
                continue;
            }
            const double reach = entranceReach(entrance);
            const bool meets =
                std::min(entrance.topX, entrance.bottomX) - reach <= maxX &&
                std::max(entrance.topX, entrance.bottomX) + reach >= minX &&
                std::min(entrance.topZ, entrance.bottomZ) - reach <= maxZ &&
                std::max(entrance.topZ, entrance.bottomZ) + reach >= minZ;
            if (meets) {
                entrances.push_back(entrance);
            }
        }
    }
    return entrances;
}

double CaveField::entranceReach(const CaveEntrance& entrance) const {
    return largestRadius(entrance) + kEntranceReachMargin;
}

bool CaveField::nearEntrance(double x, double z, std::span<const CaveEntrance> entrances,
                             double margin) {
    return std::any_of(entrances.begin(), entrances.end(), [&](const CaveEntrance& entrance) {
        return entranceAxisDistance(entrance, x, z) <= largestRadius(entrance) + margin;
    });
}

float CaveField::entranceVoid(const CaveColumn& column, double y,
                              std::span<const CaveEntrance> entrances) const {
    float result = params_.voidClamp;
    for (int i = 0; i < column.entranceCount; ++i) {
        const CaveEntrance& entrance =
            entrances[static_cast<std::size_t>(column.entrances[static_cast<std::size_t>(i)])];
        result = std::min(result, entranceVoidAt(entrance, column.x, y, column.z));
    }
    return result;
}

} // namespace terrain

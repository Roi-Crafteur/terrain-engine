#include "walkable_ground.hpp"

#include "place_pose.hpp"

#include <algorithm>
#include <cmath>

namespace terrain::unreal {

namespace {

// One turn, radians: a function, not a constant of the namespace, which a local of another file
// of the same unity unit of Unreal would hide (C4459).
double fullTurn() {
    return 2.0 * 3.14159265358979323846;
}

// The points of a ring of radius r, about step meters apart, at least 8 (one: the column).
int ringPoints(double r, double step) {
    if (r <= 0.0) {
        return 1;
    }
    return std::max(8, static_cast<int>(std::ceil(fullTurn() * r / step)));
}

} // namespace

double groundSlope(double x, double z, double probe, const GroundHeight& height) {
    const double here = height(x, z);
    const double around[4] = {height(x + probe, z), height(x - probe, z), height(x, z + probe),
                              height(x, z - probe)};
    double steepest = 0.0;
    for (const double h : around) {
        steepest = std::max(steepest, std::abs(h - here) / probe);
    }
    return steepest;
}

double walkableSlope(double walkableAngleDegrees, double marginDegrees) {
    const double angle = std::clamp(walkableAngleDegrees - marginDegrees, 0.0, 89.0);
    return std::tan(angle * fullTurn() / 360.0);
}

double walkableNormalUp(double walkableAngleDegrees, double marginDegrees) {
    const double angle = std::clamp(walkableAngleDegrees - marginDegrees, 0.0, 89.0);
    return std::cos(angle * fullTurn() / 360.0);
}

const char* walkableReasonText(WalkableReason reason) {
    switch (reason) {
    case WalkableReason::Found:
        return "walkable ground found";
    case WalkableReason::NoDryGround:
        return "no dry ground: water everywhere around";
    case WalkableReason::TooSteep:
        return "the dry ground around is too steep";
    case WalkableReason::BadSearch:
        break;
    }
    return "no search (its radius, step or probe is not positive)";
}

WalkableCandidates walkableCandidates(double x, double z, const WalkableSearch& search,
                                      const GroundHeight& height, const GroundWet& wet,
                                      std::size_t maxCount) {
    WalkableCandidates out;
    if (!(search.ringStep > 0.0) || !(search.radius >= 0.0) || !(search.probe > 0.0) ||
        maxCount == 0) {
        return out;
    }
    bool anyDry = false;
    const int rings = static_cast<int>(std::floor(search.radius / search.ringStep + 1e-9));
    for (int ring = 0; ring <= rings && out.spots.size() < maxCount; ++ring) {
        const double r = ring * search.ringStep;
        const int points = ringPoints(r, search.ringStep);
        std::vector<WalkableSpot> found;
        for (int i = 0; i < points; ++i) {
            // From the north (-z) clockwise, as the headings.
            const double a = fullTurn() * i / points;
            const double px = x + r * std::sin(a);
            const double pz = z - r * std::cos(a);
            if (wet(px, pz)) {
                continue;
            }
            anyDry = true;
            const double slope = groundSlope(px, pz, search.probe, height);
            if (slope <= search.maxSlope) {
                found.push_back({px, pz, height(px, pz), slope, r});
            }
        }
        std::stable_sort(found.begin(), found.end(), [](const WalkableSpot& a,
                                                         const WalkableSpot& b) {
            return a.slope < b.slope;
        });
        out.spots.insert(out.spots.end(), found.begin(), found.end());
    }
    if (out.spots.size() > maxCount) {
        out.spots.resize(maxCount);
    }
    out.reason = !out.spots.empty() ? WalkableReason::Found
                 : anyDry           ? WalkableReason::TooSteep
                                    : WalkableReason::NoDryGround;
    return out;
}

std::optional<WalkableSpot> nearestWalkable(double x, double z, const WalkableSearch& search,
                                            const GroundHeight& height, const GroundWet& wet) {
    const WalkableCandidates candidates = walkableCandidates(x, z, search, height, wet, 1);
    if (candidates.spots.empty()) {
        return std::nullopt;
    }
    return candidates.spots.front();
}

std::optional<double> waterSurfaceAround(double x, double z, double radius,
                                         const GroundSample& sample) {
    std::optional<double> surface;
    for (int i = -1; i < 8; ++i) {
        const double a = fullTurn() * i / 8.0;
        const double px = i < 0 ? x : x + radius * std::sin(a);
        const double pz = i < 0 ? z : z - radius * std::cos(a);
        const HeightSample s = sample(px, pz);
        if (columnHasWater(s)) {
            surface = std::max(surface.value_or(s.waterLevel), static_cast<double>(s.waterLevel));
        }
    }
    return surface;
}

const char* standRefusalText(StandRefusal refusal) {
    switch (refusal) {
    case StandRefusal::None:
        return "stands";
    case StandRefusal::NoCollision:
        return "no collision under it (the tiles not loaded there?)";
    case StandRefusal::TooSteep:
        return "its collision is too steep";
    case StandRefusal::Step:
        return "its collision has a step higher than the pawn climbs";
    case StandRefusal::OffHeightmap:
        return "its collision is far from the heightmap (a cliff, an overhang, a hollow)";
    case StandRefusal::UnderWater:
        return "its collision is under the water of the core";
    }
    return "unknown";
}

StandCheck checkStand(std::span<const FloorProbe> probes, double heightmapGround,
                      std::optional<double> waterSurface, const StandLimits& limits) {
    StandCheck check;
    if (probes.empty() || std::any_of(probes.begin(), probes.end(),
                                      [](const FloorProbe& p) { return !p.hit; })) {
        return check; // NoCollision
    }
    double lowest = probes.front().altitude;
    double highest = lowest;
    for (const FloorProbe& p : probes) {
        if (p.normalUp < limits.minNormalUp) {
            check.refusal = StandRefusal::TooSteep;
            return check;
        }
        lowest = std::min(lowest, p.altitude);
        highest = std::max(highest, p.altitude);
    }
    check.altitude = highest;
    if (highest - lowest > limits.maxStep) {
        check.refusal = StandRefusal::Step;
    } else if (std::abs(probes.front().altitude - heightmapGround) > limits.maxOffHeightmap) {
        check.refusal = StandRefusal::OffHeightmap;
    } else if (waterSurface && lowest < *waterSurface) {
        check.refusal = StandRefusal::UnderWater;
    } else {
        check.refusal = StandRefusal::None;
    }
    return check;
}

} // namespace terrain::unreal

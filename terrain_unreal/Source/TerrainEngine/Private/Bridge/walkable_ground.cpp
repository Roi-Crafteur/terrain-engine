#include "walkable_ground.hpp"

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

std::optional<WalkableSpot> nearestWalkable(double x, double z, const WalkableSearch& search,
                                            const GroundHeight& height, const GroundWet& wet) {
    if (!(search.ringStep > 0.0) || !(search.radius >= 0.0) || !(search.probe > 0.0)) {
        return std::nullopt;
    }
    const int rings = static_cast<int>(std::floor(search.radius / search.ringStep + 1e-9));
    for (int ring = 0; ring <= rings; ++ring) {
        const double r = ring * search.ringStep;
        const int points = ringPoints(r, search.ringStep);
        std::optional<WalkableSpot> best;
        for (int i = 0; i < points; ++i) {
            // From the north (-z) clockwise, as the headings.
            const double a = fullTurn() * i / points;
            const double px = x + r * std::sin(a);
            const double pz = z - r * std::cos(a);
            const double slope = groundSlope(px, pz, search.probe, height);
            if (slope > search.maxSlope || (best && slope >= best->slope) || wet(px, pz)) {
                continue;
            }
            best = WalkableSpot{px, pz, height(px, pz), slope, r};
        }
        if (best) {
            return best;
        }
    }
    return std::nullopt;
}

} // namespace terrain::unreal

// The forms of the impact craters of agent 52 (crater_forms.hpp).
#include "crater_forms.hpp"

#include "terrain/det_math.hpp"

#include <algorithm>
#include <cmath>

namespace terrain::lows {

namespace {

constexpr double kTurn = 2.0 * 3.14159265358979323846;

// Fresh: the depth of the simple craters (a share of the diameter) and of the complex ones
// (meters at 1 km across, times the diameter in kilometers to a power), the rim (a share of the
// diameter, of the depth at most), the floor (a share of the radius).
constexpr double kSimpleDepth = 0.16;
constexpr double kComplexDepth = 400.0;
constexpr double kComplexPower = 0.3;
constexpr double kRimShare = 0.036;
constexpr double kRimDepth = 0.25;
constexpr double kSimpleFloor = 0.2;
constexpr double kComplexFloor = 0.5;
constexpr double kComplexFloorMore = 0.1; // from 4 to 20 km across
// The central peak grows in from kPeakFrom to kPeakFull across (a share of the depth, of the
// radius); the ring of peaks takes its place from kRingFrom to kRingFull.
constexpr double kPeakFrom = 4'000.0;
constexpr double kPeakFull = 8'000.0;
constexpr double kPeakShare = 0.35;
constexpr double kPeakRadius = 0.2;
constexpr double kRingFrom = 40'000.0;
constexpr double kRingFull = 55'000.0;
constexpr double kRingShare = 0.25;
constexpr double kRingAt = 0.45;
constexpr double kRingWidth = 0.12;
// The terraces: their count from 6 km across, the share of a step taken by its scarp, how far
// their scarps wander around the crater (a share of a step; under 1 / 4: the wall still rises).
constexpr double kTerracesFrom = 6'000.0;
constexpr double kFaceShare = 0.3;
constexpr double kWander = 0.2;
// The wear: what it takes of the rim, of the depth (sediment, faster at first), of the peak and
// the ring; how far it widens the floor toward kFloorWorn; the terraces fresh; the share of the
// detail the sediment buries.
constexpr double kRimWear = 0.8;
constexpr double kFill = 0.92;
constexpr double kFillPower = 0.6;
constexpr double kPeakWear = 0.6;
constexpr double kRingWear = 0.6;
constexpr double kFloorWorn = 0.8;
constexpr double kFloorWiden = 0.6;
constexpr double kTerraceFresh = 0.7;
constexpr double kSediment = 0.8;
// The rim decays as the cube of the distance from kFadeFrom radii to nothing at kCraterOuter.
constexpr double kFadeFrom = 1.5;
// Where the relief of the plains goes: from kInsideFrom radii to the crest and a little beyond.
constexpr double kInsideFrom = 0.7;
constexpr double kInsideTo = 1.05;

double smooth(double edge0, double edge1, double x) {
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double bump(double u) {
    const double v = 1.0 - u * u;
    return v > 0.0 ? v * v : 0.0;
}

// A wall in steps: n scarps over benches, from 0 at the floor to 1 at the crest, the scarps
// shifted by wander steps at mid-wall (slumped arcs, not rings).
double terraced(double t, int n, double wander) {
    const double s = t * n + 4.0 * wander * t * (1.0 - t);
    const double k = std::floor(s);
    return (k + smooth(0.0, kFaceShare, s - k)) / n;
}

// The crater as its wear leaves it.
struct Worn {
    double rim = 0.0;
    double floorLevel = 0.0; // under the ground around
    double peak = 0.0;
    double ring = 0.0;
    double floorShare = 0.0;
    double terrace = 0.0;
    double sediment = 0.0;
};

Worn worn(const Crater& c) {
    const double w = std::clamp(c.wear, 0.0, 1.0);
    Worn o;
    o.rim = c.rim * (1.0 - kRimWear * w);
    const double fill = w > 0.0 ? kFill * dmath::pow(w, kFillPower) : 0.0;
    o.floorLevel = o.rim - c.depth * (1.0 - fill);
    o.peak = c.peak * (1.0 - kPeakWear * w);
    o.ring = c.ring * (1.0 - kRingWear * w);
    o.floorShare = c.floorShare + (kFloorWorn - c.floorShare) * kFloorWiden * w;
    o.terrace = kTerraceFresh * (1.0 - w);
    o.sediment = kSediment * w;
    return o;
}

} // namespace

Crater freshCrater(double diameter, double terraceDraw) {
    Crater c;
    c.radius = 0.5 * diameter;
    const double simple = kSimpleDepth * diameter;
    const double complex = kComplexDepth * dmath::pow(diameter / 1000.0, kComplexPower);
    c.depth = std::min(simple, complex);
    c.rim = std::min(kRimShare * diameter, kRimDepth * c.depth);
    if (!(complex < simple)) {
        c.floorShare = kSimpleFloor;
        return c;
    }
    c.floorShare = kComplexFloor + kComplexFloorMore * smooth(4'000.0, 20'000.0, diameter);
    const double ring = smooth(kRingFrom, kRingFull, diameter);
    c.peak = kPeakShare * c.depth * smooth(kPeakFrom, kPeakFull, diameter) * (1.0 - ring);
    c.ring = kRingShare * c.depth * ring;
    c.terraces = diameter < kTerracesFrom ? (terraceDraw < 0.5 ? 0 : 1)
                                          : 2 + static_cast<int>(terraceDraw * 3.0);
    return c;
}

double craterFloor(const Crater& c) {
    return worn(c).floorLevel;
}

double crestRadius(const Crater& c, double turns) {
    double r = 1.0;
    for (std::size_t k = 0; k < 3; ++k) {
        r += c.harmonics[k] *
             dmath::cosTurns(static_cast<double>(k + 2) * (turns - c.phases[k]));
    }
    return c.radius * r;
}

double craterReach(const Crater& c) {
    return kCraterOuter * c.radius * (1.0 + c.harmonics[0] + c.harmonics[1] + c.harmonics[2]);
}

CraterShape shapeAt(const Crater& c, double dx, double dz) {
    const double d = dmath::hypot(dx, dz);
    if (!(d < c.reach)) {
        return {};
    }
    double turns = d > 0.0 ? dmath::atan2(dz, dx) / kTurn : 0.0;
    turns = turns < 0.0 ? turns + 1.0 : turns;
    const double rho = d / crestRadius(c, turns);
    const Worn w = worn(c);
    const double crest =
        w.rim * (1.0 + c.harmonics[3] * dmath::cosTurns(6.0 * (turns - c.phases[3])));
    CraterShape out;
    out.inside = 1.0 - smooth(kInsideFrom, kInsideTo, rho);
    if (rho >= 1.0) {
        out.delta = crest / (rho * rho * rho) * (1.0 - smooth(kFadeFrom, kCraterOuter, rho));
        return out;
    }
    double h = w.floorLevel;
    if (rho > w.floorShare) {
        double t = (rho - w.floorShare) / (1.0 - w.floorShare);
        if (c.terraces > 0) {
            const double wander =
                kWander * (0.6 * dmath::cosTurns(5.0 * (turns - c.phases[1])) +
                           0.4 * dmath::cosTurns(7.0 * (turns - c.phases[2])));
            t += w.terrace * (terraced(t, c.terraces, wander) - t);
        }
        const double wall = c.floorShare < 0.35 ? t * t : t * std::sqrt(t);
        h += (crest - w.floorLevel) * wall;
    }
    h += w.peak * bump(rho / kPeakRadius) + w.ring * bump((rho - kRingAt) / kRingWidth);
    out.delta = h;
    out.onFloor = 1.0 - smooth(0.85 * w.floorShare, w.floorShare, rho);
    out.flat = w.sediment * out.onFloor;
    return out;
}

} // namespace terrain::lows

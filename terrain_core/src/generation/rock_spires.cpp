#include "rock_spires.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace terrain::rock_spires {

namespace {

constexpr double kPi = 3.14159265358979323846;
// Their edges rounded over kEdge meters, its top over kTopEdge (a twentieth of its height at
// most), its pillars joined over kJoin.
constexpr double kEdge = 0.35;
constexpr double kTopEdge = 0.4;
constexpr double kJoin = 0.6;
// The details fade out from kFeatureLow to kFeatureHigh cells (the faces: rock_faces.cpp), the
// passage of an arch from kPassageLow to kPassageHigh cells wide.
constexpr double kFeatureLow = 1.5;
constexpr double kFeatureHigh = 3.0;
constexpr double kPassageLow = 1.0;
constexpr double kPassageHigh = 2.0;
// Its beds are horizontal (the lattice of the tiles in y: no teeth along them), shown from kBedLow
// to kBedHigh cells thick; each steps to the next over its last kBedEdge.
constexpr double kBedLow = 0.75;
constexpr double kBedHigh = 1.5;
constexpr double kBedEdge = 0.08;
// Nothing stands out above kTopKeep of its height (no mushroom) nor within kOverhangOver meters
// over its notch.
constexpr double kTopKeep = 0.6;
constexpr double kOverhangOver = 4.0;
// A lamina grooved at its foot over kGroove of its thickness.
constexpr double kGroove = 0.3;
// The notch reaches from kNotchLow meters (under the sea) up to its top.
constexpr double kNotchLow = -1.5;
// A step of its top over kStepEdge meters.
constexpr double kStepEdge = 0.3;
// Around the foot, the platform from kApronOpen to kApronFull of the cosine of the angle from its
// side: none over about a third of the way around.
constexpr double kApronOpen = -0.3;
constexpr double kApronFull = 0.6;
// Its blocks stand from kBlockFoot meters (under the sea) up to their top.
constexpr double kBlockFoot = -6.0;

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double smoothMax(double a, double b, double k) {
    const double h = std::max(k - std::abs(a - b), 0.0) / k;
    return std::max(a, b) + 0.25 * k * h * h;
}

double smoothMin(double a, double b, double k) {
    return -smoothMax(-a, -b, k);
}

// How much a detail size meters wide shows on cells of cell meters.
double shown(double size, double cell) {
    return smooth(kFeatureLow * cell, kFeatureHigh * cell, size);
}

// Over its foot, up to kOverhangOver meters over its notch, nothing juts over the rock below
// (a nook a pawn would not stand in): from 0 there to 1 kOverhangOver meters higher.
double lowShare(const Shape& s, double y) {
    return smooth(s.notchTop + kOverhangOver, s.notchTop + 2.0 * kOverhangOver, y);
}

// Turns wrapped into [-0.5, 0.5).
double wrapTurns(double t) {
    return t - std::floor(t + 0.5);
}

// How much narrower a pillar stands at height y: 1 at the sea level and below.
double taperAt(const Pillar& p, double y) {
    const double t = std::clamp(y / p.height, 0.0, 1.0);
    return 1.0 - p.taper * dmath::pow(t, p.power);
}

// The distances of the faces of a pillar from its axis at height y, as cells of cell meters draw
// them: in its beds (each from its bottom, stepping to the next over its last kBedEdge), the beds
// thinner than a few cells smoothed into its taper.
struct Faces {
    std::array<double, kMaxSides> reach{};
};

Faces facesAt(const Shape& s, const Pillar& p, double y, double cell) {
    Faces out;
    const double smoothShrink = taperAt(p, y);
    for (int i = 0; i < p.sides; ++i) {
        out.reach[static_cast<std::size_t>(i)] =
            p.radius * p.faceReach[static_cast<std::size_t>(i)] * smoothShrink;
    }
    if (s.bedCount == 0 || !(y > s.bedBottom[0])) {
        return out;
    }
    const double* first = s.bedBottom.data();
    const auto k = static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(
        std::upper_bound(first, first + s.bedCount, y) - first - 1, 0, s.bedCount - 1));
    const double low = s.bedBottom[k];
    const double thick = s.bedBottom[k + 1] - low;
    const double f = std::clamp((y - low) / thick, 0.0, 1.0);
    const double show = smooth(kBedLow * cell, kBedHigh * cell, thick);
    // Out only between kOverhangOver meters over its notch and kTopKeep of its height.
    const double keep = (1.0 - smooth(kTopKeep, 1.0, y / p.height)) * lowShare(s, y);
    const std::size_t next = std::min(k + 1, static_cast<std::size_t>(s.bedCount - 1));
    const double step = smooth(1.0 - kBedEdge, 1.0, f);
    const auto outOf = [&](std::size_t bed) {
        return s.bedOut[bed] > 0.0 ? s.bedOut[bed] * keep : s.bedOut[bed];
    };
    const double shrinkHere = taperAt(p, low);
    const double shrinkNext = taperAt(p, s.bedBottom[k + 1]);
    for (int i = 0; i < p.sides; ++i) {
        const auto j = static_cast<std::size_t>(i);
        const double base = p.radius * p.faceReach[j];
        const double here = base * shrinkHere + outOf(k) + s.bedFace[k][j];
        const double there = base * shrinkNext + outOf(next) + s.bedFace[next][j];
        const double beds = here + (there - here) * step;
        out.reach[j] += show * (beds - out.reach[j]);
    }
    return out;
}

// A point (x, z) from the axis of a pillar: its coordinates along the axis of the spire and across
// it, its distance from the axis and its direction there.
struct Framed {
    double u = 0.0;
    double v = 0.0;
    double r = 0.0;
    double turns = 0.0;
};

Framed framed(const Shape& s, double x, double z) {
    const dmath::SineCosine axis = dmath::sinCosTurns(s.axis);
    const double u = x * axis.cosine + z * axis.sine;
    const double v = z * axis.cosine - x * axis.sine;
    const double r = dmath::hypot(u, v);
    return {u, v, r, r > 0.0 ? s.axis + dmath::atan2(v, u) / (2.0 * kPi) : 0.0};
}

// What carves its sides at f, y: its laminae, its notch (where the sea beats its foot only: none
// over its platform), its cracks.
double carvedAt(const Shape& s, const Pillar& p, const Framed& f, double y, double cell) {
    double carve = 0.0;
    if (s.bedCount > 0 && y > s.bedBottom[0] && y < p.height - s.lamina) {
        const double t = y / s.lamina + 0.37 * dmath::sinTurns(y / (5.3 * s.lamina));
        carve += shown(s.lamina, cell) * s.laminaDepth *
                 (1.0 - smooth(0.0, kGroove, t - std::floor(t)));
    }
    const double half = 0.5 * (s.notchTop - kNotchLow);
    const double middle = kNotchLow + half;
    if (std::abs(y - middle) < half) {
        const double sea = s.apron > 0.0 ? 1.0 - apronAt(s, f.turns) / s.apron : 1.0;
        const double u = (y - middle) / half;
        carve += shown(std::min(s.notchDepth, 2.0 * half), cell) * s.notchDepth * sea *
                 std::sqrt(1.0 - u * u);
    }
    for (int i = 0; i < s.crackCount; ++i) {
        const Crack& c = s.cracks[static_cast<std::size_t>(i)];
        if (!(y > c.low) || !(y < c.high)) {
            continue;
        }
        const double rise = (y - c.low) / (c.high - c.low);
        const double toward = c.turns + c.wander * dmath::sinTurns(1.3 * rise + c.phase);
        const double width = c.width * (0.6 + 0.4 * dmath::sinTurns(2.1 * rise + c.phase));
        const double arc = std::abs(wrapTurns(f.turns - toward)) * 2.0 * kPi * f.r;
        carve += shown(c.width, cell) * c.depth * taperAt(p, y) *
                 (1.0 - smooth(0.0, 0.5 * width, arc)) * smooth(0.0, 0.15, rise) *
                 (1.0 - smooth(0.7, 1.0, rise));
    }
    return carve;
}

// The top of a pillar over (x, z) meters from its axis: a plane, tilted; its main pillar in steps.
double topOf(const Shape& s, const Pillar& p, double x, double z, bool main) {
    const dmath::SineCosine tilt = dmath::sinCosTurns(s.tiltTurns);
    double top = p.height * (1.0 + s.topTilt * (x * tilt.cosine + z * tilt.sine) / p.radius);
    if (main && s.topStep > 0.0) {
        const dmath::SineCosine step = dmath::sinCosTurns(s.stepTurns);
        const double across = x * step.cosine + z * step.sine - s.stepAt * p.radius;
        top -= s.topStep * smooth(-kStepEdge, kStepEdge, across);
    }
    return top;
}

double pillarDensity(const Shape& s, const Pillar& p, double dx, double dz, double y,
                     double cell, bool main) {
    const double up = std::max(y, 0.0);
    const double x = dx - p.x - s.leanX * up;
    const double z = dz - p.z - s.leanZ * up;
    const Framed f = framed(s, x, z);
    const Faces faces = facesAt(s, p, y, cell);
    double side = 0.0;
    for (int i = 0; i < p.sides; ++i) {
        const auto j = static_cast<std::size_t>(i);
        const double v = f.u * p.faceX[j] + f.v * p.faceZ[j] - faces.reach[j];
        side = i == 0 ? v : smoothMax(side, v, kEdge);
    }
    side += carvedAt(s, p, f, y, cell);
    const double top = y - topOf(s, p, x, z, main);
    return smoothMax(side, top, std::min(kTopEdge, 0.05 * p.height));
}

// The passage of an arch at (dx, dz), y: negative in it, along the axis of the arch, as wide as
// the cells draw it (closed narrower than kPassageLow cells).
double passageAt(const Shape& s, double dx, double dz, double y, double cell) {
    const double open = smooth(kPassageLow * cell, kPassageHigh * cell, 2.0 * s.archHalf);
    if (!(open > 0.0)) {
        return 1e9;
    }
    const dmath::SineCosine axis = dmath::sinCosTurns(s.axis);
    const double along = dx * axis.cosine + dz * axis.sine;
    const double half = open * s.archHalf;
    const double u = std::min(1.0, std::abs(along) / half);
    const double vault = open * s.archHeight * std::sqrt(1.0 - u * u);
    return std::max(std::abs(along) - half, y - vault);
}

// Its blocks at (dx, dz), y: boxes with tilted tops, those narrower than a few cells shrunk away.
double blocksAt(const Shape& s, double dx, double dz, double y, double cell) {
    double d = 1e9;
    for (int i = 0; i < s.blockCount; ++i) {
        const Block& b = s.blocks[static_cast<std::size_t>(i)];
        const double size = std::min(b.halfX, b.halfZ);
        const double show = shown(2.0 * size, cell);
        if (!(show > 0.0)) {
            continue;
        }
        const dmath::SineCosine turn = dmath::sinCosTurns(b.turns);
        const double x = dx - b.x;
        const double z = dz - b.z;
        const double qx = x * turn.cosine + z * turn.sine;
        const double qz = z * turn.cosine - x * turn.sine;
        const double top = b.top + b.tiltX * qx + b.tiltZ * qz;
        const double box = std::max({std::abs(qx) - b.halfX, std::abs(qz) - b.halfZ, y - top,
                                     kBlockFoot - y});
        d = std::min(d, box + (1.0 - show) * size);
    }
    return d;
}

} // namespace

const Shape& shapeOf(const RockSpire& spire) {
    struct Last {
        RockSpire spire;
        Shape shape;
    };
    thread_local Last last{};
    if (last.spire.seed != spire.seed || last.spire.radius != spire.radius ||
        last.spire.height != spire.height || !(last.spire.radius > 0.0f)) {
        last.spire = spire;
        last.shape = shapeOf(spire.seed, static_cast<double>(spire.radius),
                             static_cast<double>(spire.height));
    }
    return last.shape;
}

double footprintAt(const Shape& s, double turns) {
    double most = 0.0;
    const dmath::SineCosine toward = dmath::sinCosTurns(turns);
    for (int i = 0; i < s.pillarCount; ++i) {
        const Pillar& p = s.pillars[static_cast<std::size_t>(i)];
        const double ahead = p.x * toward.cosine + p.z * toward.sine;
        // The nearest face toward turns.
        const Framed f = framed(s, toward.cosine, toward.sine);
        double r = 1e9;
        for (int j = 0; j < p.sides; ++j) {
            const auto k = static_cast<std::size_t>(j);
            const double facing = (f.u * p.faceX[k] + f.v * p.faceZ[k]) / f.r;
            if (facing > 0.05) {
                r = std::min(r, p.radius * p.faceReach[k] / facing);
            }
        }
        most = std::max(most, ahead + std::min(r, 2.5 * p.radius));
    }
    return most;
}

double apronAt(const Shape& s, double turns) {
    return s.apron * smooth(kApronOpen, kApronFull, dmath::cosTurns(turns - s.apronTurns));
}

double apronWidth(const Shape& s, double turns) {
    return s.apronReach * smooth(kApronOpen, kApronFull, dmath::cosTurns(turns - s.apronTurns));
}

double summitOf(const Shape& s) {
    double most = 0.0;
    for (int i = 0; i < s.pillarCount; ++i) {
        most = std::max(most,
                        s.pillars[static_cast<std::size_t>(i)].height * (1.0 + 2.0 * s.topTilt));
    }
    return most;
}

double densityAt(const Shape& s, double dx, double dz, double y, double cell) {
    double d = pillarDensity(s, s.pillars[0], dx, dz, y, cell, true);
    for (int i = 1; i < s.pillarCount; ++i) {
        const Pillar& p = s.pillars[static_cast<std::size_t>(i)];
        d = smoothMin(d, pillarDensity(s, p, dx, dz, y, cell, false), kJoin);
    }
    if (s.kind == Kind::Arch) {
        d = std::max(d, -passageAt(s, dx, dz, y, cell));
    }
    return std::min(d, blocksAt(s, dx, dz, y, cell));
}

double topAt(const Shape& s, double dx, double dz, double cell) {
    const double r = dmath::hypot(dx, dz);
    if (r > s.reach + 1.0) {
        return kBase - 1.0;
    }
    const double step = std::max(0.5, 0.5 * cell);
    double above = summitOf(s) + step;
    for (double y = above - step; y > kBase; y -= step) {
        if (densityAt(s, dx, dz, y, cell) < 0.0) {
            double below = y;
            for (int i = 0; i < 20; ++i) {
                const double middle = 0.5 * (above + below);
                (densityAt(s, dx, dz, middle, cell) < 0.0 ? below : above) = middle;
            }
            return below;
        }
        above = y;
    }
    return kBase - 1.0;
}

double topWithin(const Shape& s, double dx, double dz, double half, double cell) {
    double top = topAt(s, dx, dz, cell);
    for (int i = 0; i < s.pillarCount; ++i) {
        const Pillar& p = s.pillars[static_cast<std::size_t>(i)];
        const double tx = std::clamp(p.x + s.leanX * p.height, dx - half, dx + half);
        const double tz = std::clamp(p.z + s.leanZ * p.height, dz - half, dz + half);
        top = std::max(top, topAt(s, tx, tz, cell));
    }
    for (int k = -1; k <= 1; ++k) {
        for (int i = -1; i <= 1; ++i) {
            if (i != 0 || k != 0) {
                top = std::max(top, topAt(s, dx + i * half, dz + k * half, cell));
            }
        }
    }
    return top;
}

} // namespace terrain::rock_spires

#include "rock_spires.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace terrain::rock_spires {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::uint64_t kShapeSalt = 0x5B12E5ULL;
// The draws of the cracks, past those of the rest of the shape (shapeOf, drawPillars).
constexpr std::uint64_t kCrackDraws = 70;
// The kinds by their share: pillars below the first bound, needles, splits, arches above the last.
constexpr std::array<double, 3> kKindBounds{0.4, 0.65, 0.85};
// The harmonics 2 to 6 of the outline, at most (an arch keeps half of them: its stretch rules).
constexpr std::array<double, 5> kLobes{0.13, 0.09, 0.065, 0.05, 0.04};
// The details fade out from kFeatureLow to kFeatureHigh cells (the faces: rock_faces.cpp), the
// passage of an arch from kPassageLow to kPassageHigh cells wide.
constexpr double kFeatureLow = 1.5;
constexpr double kFeatureHigh = 3.0;
constexpr double kPassageLow = 1.0;
constexpr double kPassageHigh = 2.0;
// Its beds are horizontal (the lattice of the tiles in y: no teeth along them): drawn from
// kBedLow to kBedHigh cells.
constexpr double kBedLow = 0.75;
constexpr double kBedHigh = 1.5;
// The edges of a pillar rounded over kRound meters (kRoundShare of its height at most), its
// pillars joined over twice that.
constexpr double kRound = 1.0;
constexpr double kRoundShare = 0.05;
// A bed steps to the next over its last kBedEdge; their heights warped by kBedWarp of a bed over
// kBedWarpWave beds (no two alike); nothing bulges nor stands out above kTopKeep of the height.
constexpr double kBedEdge = 0.1;
constexpr double kBedWarp = 0.25;
constexpr double kBedWarpWave = 2.7;
constexpr double kTopKeep = 0.6;
// A bed recedes by up to its jitter, or stands out by up to kBedOut of it (a ledge on most beds,
// an overhang on a few).
constexpr double kBedOut = 0.35;
constexpr double kOverhangOver = 4.0;
// The beds begin kBedsOver meters over the notch, wholly twice that.
constexpr double kBedsOver = 1.0;
// The notch reaches from kNotchLow meters (under the sea) up to its top.
constexpr double kNotchLow = -1.5;
// The apron at most kApronShare of the height and kApronReach of the radius wide.
constexpr double kApronShare = 0.18;
constexpr double kApronReach = 0.4;
// Around the foot, the apron from kApronOpen to kApronFull of the cosine of the angle from its
// side: none over about a third of the way around.
constexpr double kApronOpen = -0.3;
constexpr double kApronFull = 0.6;

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double between(double low, double high, double t) {
    return low + (high - low) * t;
}

double smoothMax(double a, double b, double k) {
    const double h = std::max(k - std::abs(a - b), 0.0) / k;
    return std::max(a, b) + 0.25 * k * h * h;
}

double smoothMin(double a, double b, double k) {
    return -smoothMax(-a, -b, k);
}

// A draw in [0, 1) of a seed.
double drawOf(std::uint64_t seed, std::uint64_t k) {
    return static_cast<double>(noise::deriveSeed(seed, k) >> 11U) * 0x1.0p-53;
}

// How much a detail size meters wide shows on cells of cell meters.
double shown(double size, double cell) {
    return smooth(kFeatureLow * cell, kFeatureHigh * cell, size);
}

// How much it bulges (a share of its radius) at height y toward turns.
double bulgeAt(const Shape& s, double y, double turns) {
    return s.bulge *
           (0.6 * dmath::sinTurns(y / s.bulgeWave[0] + s.bulgePhase[0] +
                                  0.15 * dmath::cosTurns(turns - s.bulgePhase[2])) +
            0.4 * dmath::sinTurns(y / s.bulgeWave[1] + s.bulgePhase[1] +
                                  0.25 * dmath::cosTurns(2.0 * turns - s.bulgePhase[3])));
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

void drawPillars(Shape& s, std::uint64_t h, double radius, double height) {
    const auto d = [h](std::uint64_t k) { return drawOf(h, k); };
    switch (s.kind) {
    case Kind::Pillar:
        s.pillars[0] = {0.0, 0.0, 0.62 * radius, height, between(0.28, 0.42, d(30)),
                        between(1.0, 1.4, d(31)), between(0.05, 0.15, d(32))};
        break;
    case Kind::Needle:
        s.pillars[0] = {0.0, 0.0, 0.5 * radius, height * between(1.0, 1.15, d(30)),
                        between(0.55, 0.7, d(31)), between(0.7, 0.9, d(32)), 0.05};
        break;
    case Kind::Split: {
        const double base = 0.62 * radius;
        const double a = 0.45 * base * between(0.9, 1.1, d(30));
        const double b = 0.45 * base * between(0.9, 1.1, d(31));
        const double off = 0.5 * (a + b + 0.12 * base);
        const dmath::SineCosine axis = dmath::sinCosTurns(s.axis);
        const double tall = d(32) < 0.5 ? 1.0 : between(0.6, 0.9, d(33));
        s.pillars[0] = {0.0, 0.0, base, height * between(0.2, 0.35, d(34)), 0.08, 1.0, 0.1};
        s.pillars[1] = {off * axis.cosine, off * axis.sine, a, height * tall,
                        between(0.15, 0.3, d(35)), 1.2, 0.0};
        s.pillars[2] = {-off * axis.cosine, -off * axis.sine, b,
                        height * (tall < 1.0 ? 1.0 : between(0.6, 0.9, d(36))),
                        between(0.15, 0.3, d(37)), 1.2, 0.0};
        s.pillarCount = 3;
        break;
    }
    case Kind::Arch:
        s.pillars[0] = {0.0, 0.0, 0.55 * radius, height, between(0.4, 0.5, d(30)), 1.2, 0.35};
        s.archHalf = between(0.28, 0.36, d(31)) * 1.35 * s.pillars[0].radius;
        s.archHeight = between(0.35, 0.5, d(32)) * height;
        break;
    }
}

// The outline of a pillar toward turns (in its frame, stretched along the axis: framed), a share
// of its radius.
double outlineAt(const Shape& s, double turns) {
    double l = 1.0;
    for (std::size_t k = 0; k < s.lobe.size(); ++k) {
        l += s.lobe[k] * dmath::cosTurns(static_cast<double>(k + 2) * turns + s.phase[k]);
    }
    return l;
}

// How much narrower a pillar stands at height y: 1 at the sea level and below.
double taperAt(const Pillar& p, double y) {
    const double t = std::clamp(y / p.height, 0.0, 1.0);
    return 1.0 - p.taper * dmath::pow(t, p.power);
}

// The radius of a pillar of outline wide meters at the sea level, at height y, in its beds: each
// leans back over its height and stands out or recedes by its own draw, stepping to the next
// over its last kBedEdge; the beds narrower than a few cells smoothed into the taper.
double bedsAt(const Shape& s, const Pillar& p, double wide, double y, double turns, double cell) {
    const double smoothRadius = wide * taperAt(p, y);
    const double step = wide * p.taper * s.bed / p.height + s.bedJitter;
    // The beds over its notch only (a ledge in it would be a sill under its ceiling).
    const double show = smooth(kBedLow * cell, kBedHigh * cell, step) *
                        smooth(kBedLow * cell, kBedHigh * cell, s.bed) *
                        smooth(s.notchTop + kBedsOver, s.notchTop + 2.0 * kBedsOver, y);
    if (!(show > 0.0)) {
        return smoothRadius;
    }
    // The beds of their own thicknesses (their height warped), each receding (a ledge) or standing
    // out a little by its own draw, more on one side (its own) than on the other, never out near
    // the top (no mushroom: kTopKeep of the height).
    const double warped = y + kBedWarp * s.bed * dmath::sinTurns(y / (kBedWarpWave * s.bed));
    const dmath::FloorIndex k = dmath::floorIndex(warped / s.bed);
    const double f = warped / s.bed - static_cast<double>(k.index);
    const double bottom = y - f * s.bed;
    // Out (and leaning back within a bed) only between kOverhangOver meters over its notch and
    // kTopKeep of its height: no low nook under a bed where a pawn would not stand, no mushroom.
    const double low = lowShare(s, y);
    const double keep = (1.0 - smooth(kTopKeep, 1.0, y / p.height)) * low;
    const auto jitter = [&](std::int64_t n) {
        const auto bed = static_cast<std::uint64_t>(n);
        const double out = s.bedJitter * ((1.0 + kBedOut) * drawOf(s.bedSeed, 2 * bed) - 1.0);
        const double side = 0.6 + 0.4 * dmath::cosTurns(turns - drawOf(s.bedSeed, 2 * bed + 1));
        return out * side * (out > 0.0 ? keep : 1.0);
    };
    const double here = wide * taperAt(p, bottom) + jitter(k.index) - low * s.bedLean * f;
    const double next = wide * taperAt(p, bottom + s.bed) + jitter(k.index + 1);
    const double beds = here + (next - here) * smooth(1.0 - kBedEdge, 1.0, f);
    return smoothRadius + show * (beds - smoothRadius);
}

// The radius of a pillar toward turns at height y, r meters from its axis: its beds, less its
// notch and its cracks.
double radiusAt(const Shape& s, const Pillar& p, double turns, double r, double y, double cell) {
    // It bulges below its top only (kTopKeep of its height), above its foot (lowShare).
    const double keep = (1.0 - smooth(kTopKeep, 1.0, y / p.height)) * lowShare(s, y);
    double radius = bedsAt(s, p, p.radius * outlineAt(s, turns), y, turns, cell) *
                    (1.0 + bulgeAt(s, std::max(y, 0.0), turns) * keep);
    const double half = 0.5 * (s.notchTop - kNotchLow);
    const double middle = kNotchLow + half;
    if (std::abs(y - middle) < half) {
        // Where the sea beats its foot only: none over its apron.
        const double sea = s.apron > 0.0 ? 1.0 - apronAt(s, turns) / s.apron : 1.0;
        const double u = (y - middle) / half;
        radius -= shown(std::min(s.notchDepth, 2.0 * half), cell) * s.notchDepth * sea *
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
        const double arc = std::abs(wrapTurns(turns - toward)) * 2.0 * kPi * r;
        radius -= shown(c.width, cell) * c.depth * taperAt(p, y) *
                  (1.0 - smooth(0.0, 0.5 * width, arc)) * smooth(0.0, 0.15, rise) *
                  (1.0 - smooth(0.7, 1.0, rise));
    }
    return radius;
}

// The top of a pillar toward turns: its height, rough.
double topOf(const Shape& s, const Pillar& p, double turns) {
    const double rough = 0.6 * dmath::sinTurns(2.0 * turns + s.phase[0]) +
                         0.4 * dmath::sinTurns(3.0 * turns + s.phase[1]);
    return p.height * (1.0 + s.topRough * rough + s.topTilt * dmath::cosTurns(turns - s.tiltTurns));
}

// A point (x, z) from the axis of a pillar in its frame: its stretch along the axis of the
// spire taken out (a scale, the distance about right on its long sides: no teeth there), its
// distance from the axis and its direction there.
struct Framed {
    double r = 0.0;
    double turns = 0.0;
};

Framed framed(const Shape& s, const Pillar& p, double x, double z) {
    const dmath::SineCosine axis = dmath::sinCosTurns(s.axis);
    const double along = (x * axis.cosine + z * axis.sine) / (1.0 + p.stretch);
    const double across = z * axis.cosine - x * axis.sine;
    const double r = dmath::hypot(along, across);
    return {r, r > 0.0 ? s.axis + dmath::atan2(across, along) / (2.0 * kPi) : 0.0};
}

double pillarDensity(const Shape& s, const Pillar& p, double dx, double dz, double y,
                     double cell) {
    const double up = std::max(y, 0.0);
    const Framed f = framed(s, p, dx - p.x - s.leanX * up, dz - p.z - s.leanZ * up);
    const double side = f.r - radiusAt(s, p, f.turns, f.r, y, cell);
    const double turns = f.turns;
    const double top = y - topOf(s, p, turns);
    return smoothMax(side, top, std::min(kRound, kRoundShare * p.height));
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

} // namespace

Shape shapeOf(std::uint32_t seed, double radius, double height) {
    Shape s;
    const std::uint64_t h = noise::deriveSeed(kShapeSalt, seed);
    const auto d = [h](std::uint64_t k) { return drawOf(h, k); };
    const double kind = d(0);
    s.kind = kind < kKindBounds[0]   ? Kind::Pillar
             : kind < kKindBounds[1] ? Kind::Needle
             : kind < kKindBounds[2] ? Kind::Split
                                     : Kind::Arch;
    s.axis = d(1);
    for (std::size_t k = 0; k < s.lobe.size(); ++k) {
        s.lobe[k] = kLobes[k] * between(0.3, 1.0, d(2 + k)) * (s.kind == Kind::Arch ? 0.5 : 1.0);
        s.phase[k] = d(7 + k);
    }
    s.bed = between(6.0, 12.0, d(12));
    s.bedSeed = noise::deriveSeed(h, 13);
    s.bedJitter = between(0.8, 2.5, d(14));
    s.bedLean = between(0.2, 0.7, d(15));
    s.topRough = between(0.03, 0.08, d(16));
    s.topTilt = between(0.02, 0.06, d(40));
    s.tiltTurns = d(41);
    s.bulge = between(0.04, 0.08, d(42));
    s.bulgeWave = {between(18.0, 30.0, d(43)), between(9.0, 14.0, d(44))};
    s.bulgePhase = {d(45), d(46), d(47), d(48)};
    s.notchDepth = between(2.5, 4.5, d(17));
    s.notchTop = between(3.0, 5.0, d(18));
    drawPillars(s, h, radius, height);
    double narrowest = s.pillars[0].radius;
    for (int i = 1; i < s.pillarCount; ++i) {
        narrowest = std::min(narrowest, s.pillars[static_cast<std::size_t>(i)].radius);
    }
    s.crackCount = 1 + std::min(s.kind == Kind::Arch ? 0 : 2, static_cast<int>(3.0 * d(19)));
    for (std::size_t c = 0; c < static_cast<std::size_t>(s.crackCount); ++c) {
        const std::uint64_t k = kCrackDraws + 8 * c; // the draws of the crack
        const double low = between(0.05, 0.45, d(k + 3)) * height;
        s.cracks[c] = {d(k),
                       between(2.0, 4.0, d(k + 1)),
                       std::min(0.25 * narrowest, between(2.0, 5.0, d(k + 2))),
                       low,
                       low + between(0.3, 0.6, d(k + 4)) * height,
                       between(0.01, 0.03, d(k + 5)),
                       d(k + 6)};
    }
    const dmath::SineCosine lean = dmath::sinCosTurns(d(49));
    const double leaning = between(0.02, 0.08, d(50)) * (s.kind == Kind::Needle ? 1.5 : 1.0);
    s.leanX = leaning * lean.cosine;
    s.leanZ = leaning * lean.sine;
    s.apron = std::min(kApronShare * height, kApronSlope * kApronReach * radius);
    s.apronTurns = d(51);
    double lobes = 0.0;
    for (const double l : s.lobe) {
        lobes += l;
    }
    for (int i = 0; i < s.pillarCount; ++i) {
        const Pillar& p = s.pillars[static_cast<std::size_t>(i)];
        s.reach = std::max(s.reach, dmath::hypot(p.x, p.z) + leaning * summitOf(s) +
                                        p.radius * (1.0 + lobes + p.stretch) * (1.0 + s.bulge) +
                                        s.bedJitter);
    }
    return s;
}

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
        // A meter toward turns is f.r meters in the frame of the pillar.
        const Framed f = framed(s, p, toward.cosine, toward.sine);
        most = std::max(most, ahead + p.radius * outlineAt(s, f.turns) *
                                          (1.0 + bulgeAt(s, 0.0, f.turns)) / f.r);
    }
    return most;
}

double apronAt(const Shape& s, double turns) {
    return s.apron * smooth(kApronOpen, kApronFull, dmath::cosTurns(turns - s.apronTurns));
}

double apronWidth(const Shape& s, double turns) {
    return apronAt(s, turns) / kApronSlope;
}

double summitOf(const Shape& s) {
    double most = 0.0;
    for (int i = 0; i < s.pillarCount; ++i) {
        most = std::max(most, s.pillars[static_cast<std::size_t>(i)].height *
                                  (1.0 + s.topRough + s.topTilt));
    }
    return most;
}

double densityAt(const Shape& s, double dx, double dz, double y, double cell) {
    double d = pillarDensity(s, s.pillars[0], dx, dz, y, cell);
    for (int i = 1; i < s.pillarCount; ++i) {
        d = smoothMin(d, pillarDensity(s, s.pillars[static_cast<std::size_t>(i)], dx, dz, y, cell),
                      2.0 * kRound);
    }
    if (s.kind == Kind::Arch) {
        d = std::max(d, -passageAt(s, dx, dz, y, cell));
    }
    return d;
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

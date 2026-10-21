#include "valleys.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace terrain::valleys {

namespace {

constexpr std::array<double, 2> kSpacings = {32'768.0, 16'384.0};
constexpr std::array<float, 2> kDepths = {0.8f, 0.6f};

// Valleys fade in between these regional slopes (meters per meter): none on plateaus and divides.
constexpr double kFlatSlope = 2.0e-4;
constexpr double kSteepSlope = 1.5e-3;
// The valleys of an anchor turn by up to about 35 degrees from the direction, and are kSpread
// times closer or farther apart than the spacing.
constexpr double kTwist = 0.7;
constexpr double kSpread = 0.3;
// How much the direction of the next octave turns down the walls of the valleys before it (up
// to half of it at mid-slope, where their profile is steepest).
constexpr double kTributary = 1.0;
// The valleys of a quarter of the anchors are missing (tributaries begin and end) and the others
// are kDepthLow to 1 times as deep.
constexpr double kMissing = 0.25;
constexpr double kDepthLow = 0.6;
// The valleys fade out where the norm of the blend of the anchors falls between these.
constexpr double kCancelLow = 0.05;
constexpr double kCancelHigh = 0.35;
// Anchors lie within this share of a cell from its center.
constexpr double kJitter = 0.35;
// The walls of the valleys are straight from their floor to the sharp ridge between them, or
// steeper at the bottom by up to kConcave (the ridges stay sharp: no hollow along them).
constexpr double kConcave = 0.6;

struct Direction {
    double x;
    double z;
};

Direction normalized(double x, double z) {
    const double n = dmath::hypot(x, z);
    return n > 0.0 ? Direction{x / n, z / n} : Direction{1.0, 0.0};
}

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double quintic(double t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

// sin(2 pi t), approximated by parabolas: C1, periodic, exactly 0 and +-1 at the quarters.
double parabolicSine(double t) {
    const double f = t - std::floor(t);
    return f < 0.5 ? 16.0 * f * (0.5 - f) : -16.0 * (f - 0.5) * (1.0 - f);
}

std::uint64_t wrapCell(int i, int cells) {
    return static_cast<std::uint64_t>((i % cells + cells) % cells);
}

// Field k (16 bits) of a hash, in [0, 1).
double field(std::uint64_t hash, int k) {
    return static_cast<double>((hash >> (16 * k)) & 0xFFFFU) / 65536.0;
}

// Byte k of a hash, in [0, 1).
double byte(std::uint64_t hash, int k) {
    return static_cast<double>((hash >> (8 * k)) & 0xFFU) / 256.0;
}

// The distance across the valleys from the cos c and sin s of the phase, normalized: 0 on the
// axis of a valley, 1 on the ridge between two, linear in the phase near both (a diamond angle).
double across(double c, double s) {
    const double ratio = std::abs(s) / (std::abs(c) + std::abs(s));
    return 0.5 * (c >= 0.0 ? ratio : 2.0 - ratio);
}

// The valleys of the four anchors around (x, z) for one octave, across the direction u turned by
// each anchor (straight valleys per anchor: their blend bends): the blended cos and sin of their
// phases, and their blended concavity and depth.
struct Blend {
    double c = 0.0;
    double s = 0.0;
    double concave = 0.0;
    double depth = 0.0;
};

Blend blend(const Octave& o, const WorldSize& world, double x, double z, Direction u) {
    // In cells, cell i centered on i.
    const double cx = (x + 0.5 * world.x) / o.spacing - 0.5;
    const double cz = (z + 0.5 * world.z) / o.spacing - 0.5;
    const double fx = std::floor(cx);
    const double fz = std::floor(cz);
    const double wx = quintic(cx - fx);
    const double wz = quintic(cz - fz);
    Blend b;
    for (int k = 0; k < 4; ++k) {
        const int di = k & 1;
        const int dj = k >> 1;
        const int i = static_cast<int>(fx) + di;
        const int j = static_cast<int>(fz) + dj;
        const std::uint64_t hash = noise::deriveSeed(
            o.seed, wrapCell(j, o.cellsZ) * static_cast<std::uint64_t>(o.cellsX) +
                        wrapCell(i, o.cellsX));
        const double ox = cx - (i + kJitter * (2.0 * field(hash, 0) - 1.0));
        const double oz = cz - (j + kJitter * (2.0 * field(hash, 1) - 1.0));
        const double twist = kTwist * (2.0 * byte(hash, 6) - 1.0);
        const double frequency = 1.0 + kSpread * (2.0 * byte(hash, 7) - 1.0);
        const Direction n = normalized(-u.z + twist * u.x, u.x + twist * u.z);
        const double phase = frequency * (ox * n.x + oz * n.z); // across, in valleys
        const double w = (di != 0 ? wx : 1.0 - wx) * (dj != 0 ? wz : 1.0 - wz);
        const double kept = field(hash, 2);
        b.c += w * parabolicSine(phase + 0.25);
        b.s += w * parabolicSine(phase);
        b.concave += w * kConcave * field(hash, 2);
        if (kept >= kMissing) {
            b.depth += w * (kDepthLow + (1.0 - kDepthLow) * (kept - kMissing) / (1.0 - kMissing));
        }
    }
    return b;
}

} // namespace

Network makeNetwork(std::uint64_t seed, const WorldSize& world) {
    Network network;
    network.world = world;
    for (std::size_t k = 0; k < kSpacings.size(); ++k) {
        Octave& o = network.octaves[k];
        o.seed = noise::deriveSeed(seed, k);
        o.spacing = kSpacings[k];
        o.cellsX = std::max(1, static_cast<int>(std::lround(world.x / o.spacing)));
        o.cellsZ = std::max(1, static_cast<int>(std::lround(world.z / o.spacing)));
        o.depth = kDepths[k];
    }
    return network;
}

float carve(const Network& network, double x, double z, double downX, double downZ,
            double slope) {
    const double fade = smooth(kFlatSlope, kSteepSlope, slope);
    if (fade <= 0.0) {
        return 1.0f;
    }
    const Direction down = normalized(downX, downZ);
    Direction u = down;
    double left = 1.0;
    for (std::size_t k = 0; k < network.octaves.size(); ++k) {
        const Octave& o = network.octaves[k];
        const Blend b = blend(o, network.world, x, z, u);
        // The phase of the blend (its cos and sin normalized): where the anchors disagree the
        // valleys keep their floor, instead of being dammed by the weaker blend.
        const double norm = dmath::hypot(b.c, b.s);
        const double d = norm > 0.0 ? across(b.c, b.s) : 1.0;
        const double height = d * (1.0 + b.concave * (1.0 - d)); // 0 floor, 1 ridge
        // Where the anchors cancel out (a fork or the end of a valley) the phase spins around a
        // point: the valleys fade there instead of pinching the relief.
        const double agree = smooth(kCancelLow, kCancelHigh, norm);
        left *= 1.0 - fade * agree * static_cast<double>(o.depth) * b.depth * (1.0 - height);
        // The next octave also runs down the walls of these valleys, toward their axis: a turn
        // that varies over their spacing, slowly enough for the valleys of the next octave, from
        // the blend itself rather than its phase (no spin where the anchors cancel out).
        const double turn = kTributary * b.s * 0.5 * (1.0 - b.c);
        u = normalized(down.x + turn * down.z, down.z - turn * down.x);
    }
    return static_cast<float>(left);
}

} // namespace terrain::valleys

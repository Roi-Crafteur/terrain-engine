#include "ravines.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace terrain::ravines {

namespace {

constexpr std::array<double, 2> kSpacings = {2'048.0, 1'024.0};
constexpr std::array<float, 2> kDepths = {0.15f, 0.15f};

// The ravines of an anchor turn by up to about 25 degrees from the direction, and are kSpread
// times closer or farther apart than the spacing.
constexpr double kTwist = 0.45;
constexpr double kSpread = 0.35;
// How much the direction of the next octave turns down the walls of the ravines before it (up
// to half of it at mid-slope, where their profile is steepest).
constexpr double kTributary = 1.0;
// The ravines of a third of the anchors are missing (ravines begin and end) and the others are
// kDepthLow to 1 times as deep.
constexpr double kMissing = 0.33;
constexpr double kDepthLow = 0.4;
// The ravines fade out where the norm of the blend of the anchors falls between these: a fork or
// the end of a ravine.
constexpr double kCancelLow = 0.05;
constexpr double kCancelHigh = 0.35;
// Anchors lie within this share of a cell from its center.
constexpr double kJitter = 0.4;
// The walls of the ravines are straight from their bed to the sharp spur between them, or
// steeper at the bottom by up to kConcave: V-shaped.
constexpr double kConcave = 0.25;

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

// The distance across the ravines from the cos c and sin s of the phase, normalized: 0 on the
// axis of a ravine, 1 on the spur between two, linear in the phase near both (a diamond angle).
// The same for the direction and its opposite (s and -s).
double across(double c, double s) {
    const double ratio = std::abs(s) / (std::abs(c) + std::abs(s));
    return 0.5 * (c >= 0.0 ? ratio : 2.0 - ratio);
}

// The ravines of the four anchors around (x, z) for one octave, across the direction u turned by
// each anchor (straight ravines per anchor: their blend bends): the blended cos and sin of their
// phases, and their blended concavity and depth.
struct Blend {
    double c = 0.0;
    double s = 0.0;
    double concave = 0.0;
    double depth = 0.0;
};

double twistOf(std::uint64_t hash) {
    return kTwist * (2.0 * byte(hash, 6) - 1.0);
}

// The anchors of the cell (fx, fz), through the memo when it holds them: their hashes, and the
// inverse norm 1 / sqrt(1 + twist^2) of the direction each one turns.
const Memo::Cell& anchorsOf(const Octave& o, double fx, double fz, Memo::Cell& cell) {
    if (!cell.valid || cell.fx != fx || cell.fz != fz) {
        cell.valid = true;
        cell.fx = fx;
        cell.fz = fz;
        for (std::size_t k = 0; k < 4; ++k) {
            const int i = static_cast<int>(fx) + static_cast<int>(k & 1);
            const int j = static_cast<int>(fz) + static_cast<int>(k >> 1);
            cell.hashes[k] = noise::deriveSeed(
                o.seed, wrapCell(j, o.cellsZ) * static_cast<std::uint64_t>(o.cellsX) +
                            wrapCell(i, o.cellsX));
            const double twist = twistOf(cell.hashes[k]);
            cell.inverse[k] = 1.0 / std::sqrt(1.0 + twist * twist);
        }
    }
    return cell;
}

Blend blend(const Octave& o, const WorldSize& world, double x, double z, Direction u,
            Memo::Cell& cell) {
    // In cells, cell i centered on i.
    const double cx = (x + 0.5 * world.x) / o.spacing - 0.5;
    const double cz = (z + 0.5 * world.z) / o.spacing - 0.5;
    const double fx = std::floor(cx);
    const double fz = std::floor(cz);
    const double wx = quintic(cx - fx);
    const double wz = quintic(cz - fz);
    const Memo::Cell& anchors = anchorsOf(o, fx, fz, cell);
    Blend b;
    for (int k = 0; k < 4; ++k) {
        const int di = k & 1;
        const int dj = k >> 1;
        const int i = static_cast<int>(fx) + di;
        const int j = static_cast<int>(fz) + dj;
        const std::uint64_t hash = anchors.hashes[static_cast<std::size_t>(k)];
        const double ox = cx - (i + kJitter * (2.0 * field(hash, 0) - 1.0));
        const double oz = cz - (j + kJitter * (2.0 * field(hash, 1) - 1.0));
        const double twist = twistOf(hash);
        const double frequency = 1.0 + kSpread * (2.0 * byte(hash, 7) - 1.0);
        // u turned by the twist: its norm is sqrt(1 + twist^2), u being a unit vector.
        const double inverse = anchors.inverse[static_cast<std::size_t>(k)];
        const Direction n{(-u.z + twist * u.x) * inverse, (u.x + twist * u.z) * inverse};
        const double phase = frequency * (ox * n.x + oz * n.z); // across, in ravines
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

float depth(const Network& network, double x, double z, double slopeX, double slopeZ,
            double strength, Memo* memo) {
    const double slope = dmath::hypot(slopeX, slopeZ);
    if (!(strength > 0.0) || !(slope > 0.0)) {
        return 0.0f;
    }
    Memo local;
    Memo& cells = memo != nullptr ? *memo : local;
    const Direction down{-slopeX / slope, -slopeZ / slope};
    Direction u = down;
    double sum = 0.0;
    for (std::size_t k = 0; k < network.octaves.size(); ++k) {
        const Octave& o = network.octaves[k];
        const Blend b = blend(o, network.world, x, z, u, cells.octaves[k]);
        // The phase of the blend (its cos and sin normalized): where the anchors disagree the
        // ravines keep their bed instead of being dammed by the weaker blend.
        const double norm = dmath::hypot(b.c, b.s);
        const double d = norm > 0.0 ? across(b.c, b.s) : 1.0;
        const double height = d * (1.0 + b.concave * (1.0 - d)); // 0 bed, 1 spur
        // Where the anchors cancel out (a fork or the end of a ravine) the phase spins around a
        // point: the ravines fade there instead of pinching the slope.
        const double agree = smooth(kCancelLow, kCancelHigh, norm);
        sum += agree * static_cast<double>(o.depth) * o.spacing * b.depth * (1.0 - height);
        // The next octave also runs down the walls of these ravines, toward their axis.
        const double turn = kTributary * b.s * 0.5 * (1.0 - b.c);
        u = normalized(down.x + turn * down.z, down.z - turn * down.x);
    }
    return static_cast<float>(strength * slope * sum);
}

} // namespace terrain::ravines

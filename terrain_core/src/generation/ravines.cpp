#include "ravines.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace terrain::ravines {

namespace {

constexpr std::array<double, 2> kSpacings = {2'048.0, 1'024.0};
constexpr std::array<float, 2> kDepths = {0.20f, 0.16f};

// Each anchor draws one ravine through itself, down the slope: kLengthLow to kLengthHigh cells
// on each side of the anchor along it, kWidthLow to kWidthHigh cells on each side of its axis.
constexpr double kLengthLow = 0.6;
constexpr double kLengthHigh = 1.0;
constexpr double kWidthLow = 0.45;
constexpr double kWidthHigh = 0.6;
// Anchors lie within kJitter of a cell from its center: every ravine through a point is then
// that of an anchor of the 3 x 3 cells around it (hypot(kLengthHigh, kWidthHigh) is at most
// 1.5 - kJitter).
constexpr double kJitter = 0.3;
static_assert(kLengthHigh * kLengthHigh + kWidthHigh * kWidthHigh <=
              (1.5 - kJitter) * (1.5 - kJitter));
// A ravine deepens from its head over the first kHead of its length and fades out over the
// last kMouth.
constexpr double kHead = 0.4;
constexpr double kMouth = 0.25;
// The anchors of kMissing of the cells have no ravine (ravines begin and end at various places);
// the others are kDepthLow to 1 times as deep.
constexpr double kMissing = 0.2;
constexpr double kDepthLow = 0.4;
// The walls are steeper near the bed: the profile across is v (1 - kConcave + kConcave v), v
// from 0 on the rim to 1 on the axis.
constexpr double kConcave = 0.5;
// The next octave turns down the walls of these ravines (its direction follows their slope),
// except within kBed of their half width from the axis, where it joins them, and from the rim.
constexpr double kBed = 0.25;

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

// The anchors of the 3 x 3 cells around the cell (i, j), through the memo when it holds them.
const Memo::Cell& anchorsOf(const Octave& o, double i, double j, Memo::Cell& cell) {
    if (cell.valid && cell.i == i && cell.j == j) {
        return cell;
    }
    cell.valid = true;
    cell.i = i;
    cell.j = j;
    for (std::size_t k = 0; k < cell.anchors.size(); ++k) {
        const int di = static_cast<int>(k % 3) - 1;
        const int dj = static_cast<int>(k / 3) - 1;
        const std::uint64_t hash = noise::deriveSeed(
            o.seed, wrapCell(static_cast<int>(j) + dj, o.cellsZ) *
                            static_cast<std::uint64_t>(o.cellsX) +
                        wrapCell(static_cast<int>(i) + di, o.cellsX));
        Memo::Anchor& a = cell.anchors[k];
        a.x = di + kJitter * (2.0 * field(hash, 0) - 1.0);
        a.z = dj + kJitter * (2.0 * field(hash, 1) - 1.0);
        const double kept = field(hash, 2);
        a.depth = kept < kMissing ? 0.0 : kDepthLow + (1.0 - kDepthLow) * (kept - kMissing) /
                                                          (1.0 - kMissing);
        a.length = kLengthLow + (kLengthHigh - kLengthLow) * byte(hash, 6);
        a.width = kWidthLow + (kWidthHigh - kWidthLow) * byte(hash, 7);
    }
    return cell;
}

// The ravines of one octave around (x, z), running down u: the depth of the deepest, as a share
// of the depth of the octave, and the turn of the slope down their walls toward their axis
// (across u, meters per meter of the slope).
struct Carve {
    double depth = 0.0;
    double turn = 0.0;
};

Carve carve(const Octave& o, const WorldSize& world, double x, double z, Direction u,
            Memo::Cell& cell) {
    // In cells, cell i centered on i.
    const double cx = (x + 0.5 * world.x) / o.spacing - 0.5;
    const double cz = (z + 0.5 * world.z) / o.spacing - 0.5;
    const double i = std::floor(cx + 0.5);
    const double j = std::floor(cz + 0.5);
    const Memo::Cell& anchors = anchorsOf(o, i, j, cell);
    const double px = cx - i;
    const double pz = cz - j;
    Carve c;
    for (const Memo::Anchor& a : anchors.anchors) {
        if (!(a.depth > 0.0)) {
            continue;
        }
        const double ox = px - a.x;
        const double oz = pz - a.z;
        const double along = ox * u.x + oz * u.z; // down the slope
        const double across = oz * u.x - ox * u.z;
        const double off = std::abs(across);
        if (!(std::abs(along) < a.length) || !(off < a.width)) {
            continue;
        }
        const double t = 0.5 * (along / a.length + 1.0); // 0 at the head, 1 at the mouth
        const double taper = a.depth * smooth(0.0, kHead, t) * (1.0 - smooth(1.0 - kMouth, 1.0, t));
        const double v = 1.0 - off / a.width;
        c.depth = std::max(c.depth, taper * v * (1.0 - kConcave + kConcave * v));
        // The wall rises away from the axis: down it, toward the axis.
        const double wall = taper * (1.0 - kConcave + 2.0 * kConcave * v) / a.width *
                            smooth(0.0, kBed, 1.0 - v) * smooth(0.0, kBed, v);
        c.turn += across > 0.0 ? -wall : wall;
    }
    return c;
}

// depth, through the anchors of cells, for a slope above 0.
float depthWith(const Network& network, double x, double z, double slopeX, double slopeZ,
                double slope, double strength, Memo& cells) {
    const Direction down{-slopeX / slope, -slopeZ / slope};
    Direction u = down;
    double sum = 0.0;
    for (std::size_t k = 0; k < network.octaves.size(); ++k) {
        const Octave& o = network.octaves[k];
        const Carve c = carve(o, network.world, x, z, u, cells.octaves[k]);
        const double share = static_cast<double>(o.depth);
        sum += share * o.spacing * c.depth;
        // The next octave runs down the walls of these ravines: the slope plus theirs, across.
        const double turn = share * c.turn;
        u = normalized(down.x - turn * down.z, down.z + turn * down.x);
    }
    return static_cast<float>(strength * slope * sum);
}

} // namespace

Network makeNetwork(std::uint64_t seed, const WorldSize& world) {
    return makeNetwork(seed, world, kSpacings, kDepths);
}

Network makeNetwork(std::uint64_t seed, const WorldSize& world,
                    const std::array<double, 2>& spacings, const std::array<float, 2>& depths) {
    Network network;
    network.world = world;
    for (std::size_t k = 0; k < spacings.size(); ++k) {
        Octave& o = network.octaves[k];
        o.seed = noise::deriveSeed(seed, k);
        o.spacing = spacings[k];
        o.cellsX = std::max(1, static_cast<int>(std::lround(world.x / o.spacing)));
        o.cellsZ = std::max(1, static_cast<int>(std::lround(world.z / o.spacing)));
        o.depth = depths[k];
    }
    return network;
}

float depth(const Network& network, double x, double z, double slopeX, double slopeZ,
            double strength, Memo* memo) {
    const double slope = dmath::hypot(slopeX, slopeZ);
    if (!(strength > 0.0) || !(slope > 0.0)) {
        return 0.0f;
    }
    if (memo != nullptr) {
        return depthWith(network, x, z, slopeX, slopeZ, slope, strength, *memo);
    }
    Memo local;
    return depthWith(network, x, z, slopeX, slopeZ, slope, strength, local);
}

} // namespace terrain::ravines

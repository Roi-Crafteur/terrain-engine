// The vertical joints of the rock of the faces of the walls (wall_faces.hpp: jointAt), apart from
// the faces themselves (wall_faces.cpp).
#include "wall_faces.hpp"

#include "terrain/det_math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace terrain::wall_faces {

namespace {

// The vertical joints: the edges of cells of about kJointCell meters (as many as fit along each
// side of the world: never on the lattice of the tiles, 16 m), their sites jittered by up to
// kJointJitter of a cell; open where the noise of the joints (kJointWavelength) passes kJointOpen
// (about two thirds of their length). Each joint (the edge between two cells) draws its own
// groove: kJointTight of them tight and dark, the others open; its width from the low of its kind
// over its spread (half by its draw, half by the noise of the overhangs along it), its depth a
// ratio of its width over its spread (from kJointShallow of it along it, by the noise of the
// shifts), flat over a share of its width over its spread (smooth walls over the rest). An open
// groove is a quarter of its width deep at most: under a grazing light its far side stays lit.
// Within kJointAlone meters of where another joint meets it, its groove turns to the middle open
// one and closes (the stretches of each along the height are its own): no step where the joints
// change.
constexpr double kJointJitter = 0.45;
constexpr double kJointOpen = -0.25;
constexpr double kJointFade = 0.1;
constexpr double kJointTight = 0.2;
struct JointKind {
    double widthLow; // meters
    double widthSpread;
    double ratioLow; // of the width
    double ratioSpread;
    double flatLow; // share of the width
    double flatSpread;
};
constexpr JointKind kJointOpenKind = {4.0, 4.0, 0.12, 0.13, 0.0, 0.15};
constexpr JointKind kJointTightKind = {3.0, 1.5, 0.3, 0.1, 0.5, 0.0};
constexpr double kJointShallow = 0.6;
constexpr double kJointAlone = 2.0;
constexpr std::uint64_t kJointSalt = 0x6a6f696e745f3132ULL;

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}


// The groove of a joint: its width (and how much the noise along it adds), its depth by its width
// and the share of its width its floor lies flat over; the draw of its stretches.
struct Groove {
    double width = 0.0;
    double widthNoise = 0.0;
    double ratio = 0.0;
    double flat = 0.0;
    double seed = 0.0;
};

// The groove of a kind by draws in [0, 1) of its width, depth and floor.
Groove grooveOf(const JointKind& kind, double width, double depth, double flat) {
    return {kind.widthLow + 0.5 * kind.widthSpread * width, 0.5 * kind.widthSpread,
            kind.ratioLow + kind.ratioSpread * depth, kind.flatLow + kind.flatSpread * flat};
}

// The groove the joint between cells a and b draws (the same from either side), turned to the
// middle open one as alone runs to 0.
Groove grooveOf(const Grain& grain, std::uint64_t a, std::uint64_t b, double alone) {
    const std::uint64_t hash = noise::deriveSeed(
        noise::deriveSeed(grain.cellSeed ^ kJointSalt, std::min(a, b)), std::max(a, b));
    const auto draw = [&](unsigned k) {
        return static_cast<double>((hash >> (16U * k)) & 0xFFFFU) * 0x1.0p-16;
    };
    const JointKind& kind = draw(0) < kJointTight ? kJointTightKind : kJointOpenKind;
    const Groove drawn = grooveOf(kind, draw(1), draw(2), draw(3));
    const Groove middle = grooveOf(kJointOpenKind, 0.5, 0.5, 0.5);
    const auto mix = [&](double v, double m) { return m + alone * (v - m); };
    const double seed =
        static_cast<double>(noise::deriveSeed(hash, kJointSalt) >> 40U) * 0x1.0p-24;
    return {mix(drawn.width, middle.width), mix(drawn.widthNoise, middle.widthNoise),
            mix(drawn.ratio, middle.ratio), mix(drawn.flat, middle.flat), seed};
}

// The two nearest sites of the cells around (x, z): their distances, directions and cells, and
// the distance to the third.
struct Sites {
    std::array<double, 3> distance = {1.0e300, 1.0e300, 1.0e300};
    std::array<std::array<double, 2>, 2> to{};
    std::array<std::uint64_t, 2> cell{};
};

Sites sitesAt(const Grain& grain, double x, double z) {
    const double sizeX = grain.world.x / static_cast<double>(grain.cellsX);
    const double sizeZ = grain.world.z / static_cast<double>(grain.cellsZ);
    const double cx = (x + 0.5 * grain.world.x) / sizeX;
    const double cz = (z + 0.5 * grain.world.z) / sizeZ;
    const auto i = static_cast<std::int64_t>(std::floor(cx));
    const auto j = static_cast<std::int64_t>(std::floor(cz));
    Sites out;
    for (std::int64_t dj = -1; dj <= 1; ++dj) {
        for (std::int64_t di = -1; di <= 1; ++di) {
            const std::int64_t ci = ((i + di) % grain.cellsX + grain.cellsX) % grain.cellsX;
            const std::int64_t cj = ((j + dj) % grain.cellsZ + grain.cellsZ) % grain.cellsZ;
            const auto cell = static_cast<std::uint64_t>(cj * grain.cellsX + ci);
            const std::uint64_t hash = noise::deriveSeed(grain.cellSeed, cell);
            const double sx = static_cast<double>(di) + 0.5 +
                              kJointJitter * (2.0 * static_cast<double>(hash >> 40U) * 0x1.0p-24 - 1.0);
            const double sz = static_cast<double>(dj) + 0.5 +
                              kJointJitter *
                                  (2.0 * static_cast<double>((hash >> 16U) & 0xFF'FFFFU) * 0x1.0p-24 -
                                   1.0);
            const std::array<double, 2> to = {(cx - static_cast<double>(i) - sx) * sizeX,
                                              (cz - static_cast<double>(j) - sz) * sizeZ};
            const double d = dmath::hypot(to[0], to[1]);
            if (d < out.distance[0]) {
                out.distance = {d, out.distance[0], out.distance[1]};
                out.to = {to, out.to[0]};
                out.cell = {cell, out.cell[0]};
            } else if (d < out.distance[1]) {
                out.distance = {out.distance[0], d, out.distance[1]};
                out.to[1] = to;
                out.cell[1] = cell;
            } else if (d < out.distance[2]) {
                out.distance[2] = d;
            }
        }
    }
    return out;
}

} // namespace

Joint jointAt(const Grain& grain, double x, double z) {
    // The edge between the cells of the two nearest sites lies (F2 - F1) / 2 meters away, about
    // (an edge of the cells, never a seam of the noise).
    const Sites sites = sitesAt(grain, x, z);
    const double first = sites.distance[0];
    const double second = sites.distance[1];
    const double edge = 0.5 * (second - first);
    Joint out;
    // The gradient of the distance to the edge: half the difference of the directions from the
    // two sites.
    if (first > 0.0 && second > 0.0) {
        out.acrossX = 0.5 * (sites.to[1][0] / second - sites.to[0][0] / first);
        out.acrossZ = 0.5 * (sites.to[1][1] / second - sites.to[0][1] / first);
    }
    out.alone = smooth(0.0, kJointAlone, sites.distance[2] - second);
    const Groove groove = grooveOf(grain, sites.cell[0], sites.cell[1], out.alone);
    const double noise = static_cast<double>(grain.overhangs.fbm2D(z, x));
    out.width = groove.width + groove.widthNoise * smooth(-0.5, 0.5, noise);
    out.wall = 3.0 / (1.0 - groove.flat);
    out.seed = groove.seed;
    const double half = 0.5 * out.width;
    if (!(edge < half)) {
        return out;
    }
    const double open = static_cast<double>(grain.joints.fbm2D(x, z));
    if (!(open > kJointOpen - kJointFade)) {
        return out;
    }
    const double deep = static_cast<double>(grain.shift.fbm2D(z, x));
    out.floor = groove.ratio * out.width *
                (kJointShallow + (1.0 - kJointShallow) * smooth(-0.5, 0.5, deep)) *
                smooth(kJointOpen - kJointFade, kJointOpen + kJointFade, open);
    out.depth = out.floor * (1.0 - smooth(groove.flat * half, half, edge));
    return out;
}

} // namespace terrain::wall_faces

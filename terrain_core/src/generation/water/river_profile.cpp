// The valley of a course at a point (river_profile.hpp).
#include "river_profile.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace terrain::water {

namespace {

// The floor of the valley lies kFloorRise meters above the water at the edge of the channel.
constexpr double kFloorRise = 0.6;
// The walls stand at least kWallMargin meters beyond the channel, bounded softly over kWallSoft.
constexpr double kWallMargin = 2.0;
constexpr double kWallSoft = 6.0;
// The slope of the walls of a gorge swings by up to kGorgeSwing of itself along it.
constexpr double kGorgeSwing = 0.45;

double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// A gradient in [-1, -0.4] or [0.4, 1] for the lattice point k: no flat half wave.
double gradientOf(std::int64_t k, std::uint64_t seed) {
    const std::uint64_t bits = noise::deriveSeed(seed, static_cast<std::uint64_t>(k));
    const double u = static_cast<double>(bits >> 11) * 0x1.0p-53;
    return u < 0.5 ? -(0.4 + 1.2 * u) : 0.4 + 1.2 * (u - 0.5);
}

// The point of segment a-b nearest to (x, z): the share t along it, the distance and the unit
// direction from it to the point; and the signed distance to the line of the segment (left of the
// course positive) with how far the point lies beyond its ends along it (0 alongside), upstream of
// its start or downstream of its end.
struct Foot {
    double t = 0.0;
    double distance = 0.0;
    double side = 0.0;
    double beyond = 0.0;
    bool upstream = false;
    double upX = 1.0;
    double upZ = 0.0;
};

Foot footOn(const CoursePoint& a, const CoursePoint& b, double x, double z) {
    const double dx = b.x - a.x;
    const double dz = b.z - a.z;
    const double length2 = dx * dx + dz * dz;
    Foot f;
    const double raw = length2 > 0.0 ? ((x - a.x) * dx + (z - a.z) * dz) / length2 : 0.0;
    f.t = std::clamp(raw, 0.0, 1.0);
    const double qx = x - (a.x + f.t * dx);
    const double qz = z - (a.z + f.t * dz);
    f.distance = dmath::hypot(qx, qz);
    if (f.distance > 0.0) {
        f.upX = qx / f.distance;
        f.upZ = qz / f.distance;
    } else if (length2 > 0.0) {
        const double length = std::sqrt(length2);
        f.upX = -dz / length;
        f.upZ = dx / length;
    }
    if (length2 > 0.0) {
        const double length = std::sqrt(length2);
        f.side = (dx * (z - a.z) - dz * (x - a.x)) / length; // left of a-b: positive
        f.beyond = std::abs(raw - f.t) * length;
        f.upstream = raw < 0.0;
    }
    return f;
}

// The surface of the valley of segment a-b at a foot. Upstream of its start, the surface rises
// rise meters per meter (at least as fast as the water upstream of it): on a steep stretch the floor
// of the segments below never undercuts the banks of the water above them, while the segments
// alongside the point set its surface. The fill of the floor drops as fast downstream of its end,
// for the same reason.
Valley segmentValley(const CoursePoint& a, const CoursePoint& b, const Foot& f, double rise,
                     std::uint64_t seed, double warp) {
    const double t = f.t;
    const double level = lerp(a.level, b.level, t);
    const double surfaceLevel = f.upstream ? level + rise * f.beyond : level;
    const double fillLevel = f.upstream ? level : level - rise * f.beyond;
    const double half = lerp(a.half, b.half, t);
    const double depth = lerp(a.depth, b.depth, t);
    const double floorHalf = lerp(a.floorHalf, b.floorHalf, t);
    const double wall = lerp(a.wall, b.wall, t);
    const double foot = std::max(1.0, lerp(a.foot, b.foot, t));
    const double d = f.distance;
    // The walls, rounded at their foot, their edge with spurs and re-entrants, but never nearer
    // to the axis than kWallMargin meters beyond the channel and its meanders (a soft bound).
    const double amplitude = lerp(a.meander, b.meander, t);
    const double least = half + amplitude + kWallMargin;
    const double spur = floorHalf - warp * wallWarpOf(floorHalf) - least;
    const double out = d - least - 0.5 * (spur + std::sqrt(spur * spur + kWallSoft * kWallSoft));
    // The walls of a gorge are steeper on its spurs and gentler in its bays (the noise of the
    // walls): no even trench.
    const double steep = wall * (1.0 + kGorgeSwing * lerp(a.gorge, b.gorge, t) * warp);
    double walls = 0.0;
    double wallSlope = 0.0;
    if (out > 0.0) {
        walls = out < foot ? steep * out * out / (2.0 * foot) : steep * (out - 0.5 * foot);
        wallSlope = out < foot ? steep * out / foot : steep;
    }
    const double floorSlope = lerp(a.floorSlope, b.floorSlope, t);
    const double above = kFloorRise + floorSlope * std::max(0.0, d - half);
    const double floorLevel = surfaceLevel + above;
    // The channel about its meandering line, cut into the floor (never below it far away).
    double channelDistance = d;
    if (amplitude > 0.0) {
        const Meander m = meanderAt(lerp(a.phase, b.phase, t), amplitude, seed);
        const double rate = m.rate * lerp(a.phaseRate, b.phaseRate, t);
        // To the segment moved by the offset: continuous, even beyond its ends.
        channelDistance =
            dmath::hypot(f.beyond, (f.side - m.offset) / std::sqrt(1.0 + rate * rate));
    }
    const double channel =
        channelDistance < half
            ? surfaceLevel - depth * (1.0 - (channelDistance / half) * (channelDistance / half))
            : surfaceLevel + kBankSlope * (channelDistance - half);
    const double cut = std::max(0.0, floorLevel - channel);
    Valley v;
    v.height = floorLevel + walls - cut;
    v.level = static_cast<float>(level);
    v.wet = channelDistance < half && channel < level; // not where its surface rises upstream
    v.gorge = static_cast<float>(lerp(a.gorge, b.gorge, t));
    v.mountain = static_cast<float>(lerp(a.mountain, b.mountain, t));
    v.upX = f.upX;
    v.upZ = f.upZ;
    // The slope of the valley across it, continuous (the banks of the channel aside): the rock
    // of the walls is laid along it.
    v.slope = floorSlope + wallSlope;
    v.above = walls;
    v.floor = fillLevel + above;
    v.onFloor = 1.0 - smooth(0.0, kLeveeFade, out);
    const double edge = channelDistance - half;
    if (edge >= 0.0 && edge < kBankReach) {
        v.bank = fillLevel + kFloorRise - kBankDrop * std::max(0.0, edge - kBankFlat);
    }
    return v;
}

// The rise of the surface of segment i beyond its ends: the steepest fall of the water over it and
// the segments on each side of it, kLeastRise at least.
constexpr double kLeastRise = 4.0;

double fallOf(const CoursePoint& a, const CoursePoint& b) {
    const double run = dmath::hypot(b.x - a.x, b.z - a.z);
    return run > 0.0 ? std::max(0.0, static_cast<double>(a.level - b.level)) / run : 0.0;
}

double riseOf(const Course& c, std::uint32_t i) {
    double rise = std::max(kLeastRise, fallOf(c.points[i], c.points[i + 1]));
    if (i > 0) {
        rise = std::max(rise, fallOf(c.points[i - 1], c.points[i]));
    }
    if (i + 2 < c.points.size()) {
        rise = std::max(rise, fallOf(c.points[i + 1], c.points[i + 2]));
    }
    return rise;
}

} // namespace

Meander meanderAt(double phase, double amplitude, std::uint64_t seed) {
    const double i = std::floor(phase);
    const double f = phase - i;
    const auto k = static_cast<std::int64_t>(i);
    const double g0 = gradientOf(k, seed);
    const double g1 = gradientOf(k + 1, seed);
    const double a = g0 * f;
    const double b = g1 * (f - 1.0);
    const double w = f * f * f * (f * (6.0 * f - 15.0) + 10.0);
    const double dw = 30.0 * f * f * (f - 1.0) * (f - 1.0);
    // At most about a half of g: scaled to about 1.
    return {2.0 * amplitude * (a + w * (b - a)),
            2.0 * amplitude * (g0 + dw * (b - a) + w * (g1 - g0))};
}

void ValleyBlend::add(const Valley& v) {
    if (!(v.height < lowest_ + kBlend)) {
        return;
    }
    double w = 1.0;
    if (v.height < lowest_) {
        // A new lowest: the weights of the others halve with their height above it.
        const double scale =
            weight_ > 0.0 ? dmath::exp2((v.height - lowest_) / kBlendHalving) : 0.0;
        weight_ *= scale;
        upX_ *= scale;
        upZ_ *= scale;
        slope_ *= scale;
        above_ *= scale;
        gorge_ *= scale;
        mountain_ *= scale;
        lowest_ = v.height;
    } else {
        w = dmath::exp2((lowest_ - v.height) / kBlendHalving);
    }
    weight_ += w;
    upX_ += w * v.upX;
    upZ_ += w * v.upZ;
    slope_ += w * v.slope;
    above_ += w * v.above;
    gorge_ += w * static_cast<double>(v.gorge);
    mountain_ += w * static_cast<double>(v.mountain);
}

void ValleyBlend::applyTo(Valley& v) const {
    if (!(weight_ > 0.0)) {
        return;
    }
    const double length = dmath::hypot(upX_, upZ_);
    if (length > 1e-9) {
        v.upX = upX_ / length;
        v.upZ = upZ_ / length;
    }
    v.slope = slope_ / weight_;
    v.above = above_ / weight_;
    v.gorge = static_cast<float>(gorge_ / weight_);
    v.mountain = static_cast<float>(mountain_ / weight_);
}

Valley valleyAt(const Course& c, double x, double z, double ground, std::uint64_t meanderSeed,
                double warp) {
    Valley best;
    if (x < c.minX || x > c.maxX || z < c.minZ || z > c.maxZ) {
        return best;
    }
    const double bound = ground + kLevee;
    double lowest = bound;
    double raise = 0.0;
    double bank = -std::numeric_limits<double>::infinity();
    ValleyBlend blend;
    for (const CourseChunk& k : c.chunks) {
        const double away = dmath::hypot(x - k.x, z - k.z) - k.radius - k.flat;
        const double floorOf = static_cast<double>(k.minLevel - k.maxDepth) +
                               static_cast<double>(k.minWall) * std::max(0.0, away);
        // Beyond its floors (and their levees), a chunk only matters where it may carve below the
        // relief.
        if (away > 0.0 && !(floorOf < lowest + kBlend)) {
            continue;
        }
        for (std::uint32_t i = k.first; i < k.last; ++i) {
            const CoursePoint& a = c.points[i];
            const CoursePoint& b = c.points[i + 1];
            const Foot f = footOn(a, b, x, z);
            Valley v = segmentValley(a, b, f, riseOf(c, i), meanderSeed, warp);
            if (v.height < bound && f.distance > kReach - kReachFade - 500.0) {
                v.height += (bound - v.height) *
                            smooth(kReach - kReachFade - 500.0, kReach - 500.0, f.distance);
            }
            raise = std::max(raise, std::clamp(v.floor - ground, 0.0, kLevee) * v.onFloor);
            bank = std::max(bank, v.bank);
            if (v.height < bound + kBlend) {
                blend.add(v);
            }
            if (v.height < lowest) {
                lowest = v.height;
                best = v;
            }
        }
    }
    blend.applyTo(best);
    best.raise = raise;
    best.bank = bank;
    return best;
}

} // namespace terrain::water

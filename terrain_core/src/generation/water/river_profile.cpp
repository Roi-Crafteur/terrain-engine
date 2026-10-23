// The valley of a course at a point (river_profile.hpp).
#include "river_profile.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace terrain::water {

namespace {

// The floor of the valley lies kFloorRise meters above the water at the edge of the channel.
constexpr double kFloorRise = 0.6;

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
// course positive) with how far the point lies beyond its ends along it (0 alongside).
struct Foot {
    double t = 0.0;
    double distance = 0.0;
    double side = 0.0;
    double beyond = 0.0;
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
    }
    return f;
}

// The surface of the valley of segment a-b at a foot.
Valley segmentValley(const CoursePoint& a, const CoursePoint& b, const Foot& f,
                     std::uint64_t seed, double warp) {
    const double t = f.t;
    const double level = lerp(a.level, b.level, t);
    const double half = lerp(a.half, b.half, t);
    const double depth = lerp(a.depth, b.depth, t);
    const double floorHalf = lerp(a.floorHalf, b.floorHalf, t);
    const double wall = lerp(a.wall, b.wall, t);
    const double foot = std::max(1.0, lerp(a.foot, b.foot, t));
    const double d = f.distance;
    // The walls, rounded at their foot, their edge with spurs and re-entrants.
    const double out = d + warp * wallWarpOf(floorHalf) - floorHalf;
    double walls = 0.0;
    double wallSlope = 0.0;
    if (out > 0.0) {
        walls = out < foot ? wall * out * out / (2.0 * foot) : wall * (out - 0.5 * foot);
        wallSlope = out < foot ? wall * out / foot : wall;
    }
    const double floorSlope = lerp(a.floorSlope, b.floorSlope, t);
    const double floorLevel = level + kFloorRise + floorSlope * std::max(0.0, d - half);
    // The channel about its meandering line, cut into the floor (never below it far away).
    const double amplitude = lerp(a.meander, b.meander, t);
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
            ? level - depth * (1.0 - (channelDistance / half) * (channelDistance / half))
            : level + kBankSlope * (channelDistance - half);
    const double cut = std::max(0.0, floorLevel - channel);
    Valley v;
    v.height = floorLevel + walls - cut;
    v.level = static_cast<float>(level);
    v.wet = channelDistance < half;
    v.gorge = static_cast<float>(lerp(a.gorge, b.gorge, t));
    v.mountain = static_cast<float>(lerp(a.mountain, b.mountain, t));
    v.upX = f.upX;
    v.upZ = f.upZ;
    // The slope of the valley across it, continuous (the banks of the channel aside): the rock
    // of the walls is laid along it.
    v.slope = floorSlope + wallSlope;
    v.above = walls;
    if (!(out > 0.0)) {
        v.pond = level; // on the floor: a pond wherever the relief is below the water
    }
    return v;
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

Valley valleyAt(const Course& c, double x, double z, double bound, std::uint64_t meanderSeed,
                double warp) {
    Valley best;
    if (x < c.minX || x > c.maxX || z < c.minZ || z > c.maxZ) {
        return best;
    }
    double lowest = bound;
    double pond = -std::numeric_limits<double>::infinity();
    ValleyBlend blend;
    for (const CourseChunk& k : c.chunks) {
        const double away = dmath::hypot(x - k.x, z - k.z) - k.radius - k.flat;
        const double floorOf = static_cast<double>(k.minLevel - k.maxDepth) +
                               static_cast<double>(k.minWall) * std::max(0.0, away);
        // Beyond its floors, a chunk only matters where it may carve below the relief.
        if (away > 0.0 && !(floorOf < lowest + kBlend)) {
            continue;
        }
        for (std::uint32_t i = k.first; i < k.last; ++i) {
            const CoursePoint& a = c.points[i];
            const CoursePoint& b = c.points[i + 1];
            const Foot f = footOn(a, b, x, z);
            Valley v = segmentValley(a, b, f, meanderSeed, warp);
            if (v.height < bound && f.distance > kReach - kReachFade - 500.0) {
                v.height += (bound - v.height) *
                            smooth(kReach - kReachFade - 500.0, kReach - 500.0, f.distance);
            }
            if (v.pond > bound) {
                pond = std::max(pond, v.pond);
            }
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
    best.pond = pond;
    return best;
}

} // namespace terrain::water

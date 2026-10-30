// What of a course may change its valley at the points of a box (river_profile.hpp).
#include "river_profile.hpp"

#include "river_winding.hpp"
#include "terrain/det_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace terrain::water {

namespace {

// Every bound below holds with kMargin meters to spare: far more than the rounding of the
// distances and sums the points compute in doubles.
constexpr double kMargin = 1e-3;

// The noise of the walls never below kLowestWarp (within [-1, 1], with a margin): what bounds
// every point of a box.
constexpr double kLowestWarp = -1.01;

// What bounds the valley of segment a-b at any point (BoxCourse::Segment::mayMatter).
BoxCourse::Segment segmentOf(const CoursePoint& a, const CoursePoint& b, std::uint32_t first) {
    BoxCourse::Segment s;
    s.first = first;
    s.span = SegmentSpan(a, b);
    s.wall = std::min(a.wall, b.wall);
    s.gorge = std::max(a.gorge, b.gorge);
    s.floorSlope = std::max(a.floorSlope, b.floorSlope);
    s.keep = !(s.wall >= 0.0 && std::min(a.floorSlope, b.floorSlope) >= 0.0 && s.gorge >= 0.0 &&
               s.gorge <= 1.0 &&
               std::min({a.half, a.meander, a.floorHalf, b.half, b.meander, b.floorHalf}) >= 0.0);
    // A bowl widens the channel and deepens its bed (river_slot.hpp).
    const double half = std::max(a.half, b.half) * (1.0 + static_cast<double>(a.bowlWide));
    const double bowl = a.bowlDeep;
    const double amplitude = std::max(a.meander, b.meander);
    s.half = std::min(a.half, b.half);
    s.floorHalf = std::max(a.floorHalf, b.floorHalf);
    s.least = half + amplitude + kWallMargin;
    s.windMost = std::max(a.windMost, b.windMost);
    s.foot = std::max({1.0, static_cast<double>(a.foot), static_cast<double>(b.foot)});
    const double rate =
        kMeanderRate * amplitude * std::max(std::abs(a.phaseRate), std::abs(b.phaseRate));
    s.meander = kMeanderReach * amplitude;
    s.stretch = std::sqrt(1.0 + rate * rate);
    s.bankFrom = half + kBankReach + kMargin;
    s.widest = half;
    // Its banks above its fill, never above its floor (segmentValley).
    s.bankTop = static_cast<double>(std::max(a.valley, b.valley)) + kFloorRise;
    const auto lowOf = [bowl](const CoursePoint& p) {
        return std::min(static_cast<double>(p.valley) + kFloorRise,
                        static_cast<double>(p.level) -
                            std::max(0.0, static_cast<double>(p.depth)) - bowl);
    };
    s.low = std::min(static_cast<double>(std::min(a.valley, b.valley)) + kFloorRise,
                     static_cast<double>(std::min(a.level, b.level)) -
                         std::max(0.0, static_cast<double>(std::max(a.depth, b.depth))) - bowl);
    s.lowStart = lowOf(a);
    s.lowEnd = lowOf(b);
    s.floorTop = static_cast<double>(std::max(a.valley, b.valley)) + kFloorRise;
    return s;
}

} // namespace

// Within kBankReach of its channel its banks stand; less than kLeveeFade up its walls its floor
// may fill the relief, where it stands above it; below groundHigh + kLevee + kBlend its surface
// joins the valley. Its surface is never below the lower of its floor and of its water less the
// depth of its channel, plus its walls, which grow with the distance (segmentValley): the walls
// stand farther where their spur (floorHalf - warp wallWarpOf(floorHalf) - least) is wider,
// which it is the wider the floor, and steeper the more gorge where warp is negative. Beyond its
// ends it starts from their own, rising kLeastRise meters per meter at least upstream of its
// start.
Matters BoxCourse::Segment::mayMatter(double d, double beyond, bool upstream, double groundLow,
                                      double threshold, double warp) const {
    if (keep) {
        return Matters::Valley;
    }
    const double channel = meander > 0.0 ? (d - meander) / stretch : d;
    const Matters banks = channel * (1.0 - 1e-9) >= bankFrom ? Matters::Nothing : Matters::Banks;
    const double spur = floorHalf - warp * wallWarpOf(floorHalf) - least;
    const double out =
        d - least - 0.5 * (spur + std::sqrt(spur * spur + kWallSoft * kWallSoft)) - kMargin;
    if (!(out >= kLeveeFade + kMargin) &&
        !(floorTop + floorSlope * std::max(0.0, d - half) + kMargin <= groundLow)) {
        return Matters::Valley;
    }
    const double steep = wall * (1.0 + kGorgeSwing * gorge * std::min(0.0, warp) * 1.01);
    double walls = 0.0;
    if (out > 0.0) {
        walls = out < foot ? steep * out * out / (2.0 * foot) : steep * (out - 0.5 * foot);
    }
    double from = low;
    if (beyond > 0.0) {
        from = upstream ? lowStart + kLeastRise * beyond : lowEnd;
    }
    return from + std::max(0.0, walls) - kMargin >= threshold ? banks : Matters::Valley;
}

// The edge of its channel never nearer than its least distance (as mayMatter's) less its widest
// half width.
double BoxCourse::Segment::bankAt(double d) const {
    const double channel = meander > 0.0 ? (d - meander) / stretch : d;
    const double edge = channel * (1.0 - 1e-9) - widest;
    return bankTop - kBankDrop * std::max(0.0, edge - kBankFlat) + kMargin;
}

BoxCourse boxCourse(const Course& c, double minX, double minZ, double maxX, double maxZ,
                    double groundMin, double groundMax) {
    BoxCourse out;
    const double cx = 0.5 * (minX + maxX);
    const double cz = 0.5 * (minZ + maxZ);
    // Every point of the box lies within reach of its center.
    const double reach = 0.5 * dmath::hypot(maxX - minX, maxZ - minZ) + kMargin;
    const double threshold = groundMax + kLevee + kBlend;
    for (std::size_t n = 0; n < c.chunks.size(); ++n) {
        const CourseChunk& k = c.chunks[n];
        BoxCourse::Chunk kept;
        kept.chunk = static_cast<std::uint32_t>(n);
        kept.away = dmath::hypot(cx - k.x, cz - k.z) - reach - k.radius - k.flat;
        kept.floor = static_cast<double>(k.minLevel - k.maxDepth) +
                     static_cast<double>(k.minWall) * std::max(0.0, kept.away);
        // valleyAt skips it at every point (its lowest never above ground + kLevee).
        if (kept.away > 0.0 && !(kept.floor < groundMax + kLevee + kBlend)) {
            continue;
        }
        kept.first = static_cast<std::uint32_t>(out.segments.size());
        for (std::uint32_t i = k.first; i < k.last; ++i) {
            const CoursePoint& a = c.points[i];
            const CoursePoint& b = c.points[i + 1];
            const BoxCourse::Segment segment = segmentOf(a, b, i);
            // Beyond the same end at every point of the box, if beyond it by more than reach at
            // its center.
            const SegmentSpan& span = segment.span;
            const double raw = span.length2 > 0.0
                                   ? ((cx - a.x) * span.dx + (cz - a.z) * span.dz) / span.length2
                                   : 0.0;
            const double past = std::abs(raw - std::clamp(raw, 0.0, 1.0)) * span.length;
            const double beyond = std::max(0.0, past - reach - kMargin);
            // The side of the points of the box: that of its center, within reach.
            const double side =
                span.length > 0.0 ? (span.dx * (cz - a.z) - span.dz * (cx - a.x)) / span.length
                                  : 0.0;
            // Near a corridor, the points of the box within reach of its center along the course
            // too (along the line of the segment).
            const double along = a.s + raw * span.length;
            const double d = segment.windMost > 0.0
                                 ? windDistance(windBounds(c, along - reach - kMargin,
                                                           along + reach + kMargin),
                                                beyond, side - reach, side + reach)
                                 : std::max(0.0, distanceToSegment(cx, cz, a.x, a.z, b.x, b.z) -
                                                     reach);
            if (segment.mayMatter(d, beyond, raw < 0.0, groundMin, threshold, kLowestWarp) !=
                Matters::Nothing) {
                out.segments.push_back(segment);
            }
        }
        kept.last = static_cast<std::uint32_t>(out.segments.size());
        if (kept.last > kept.first) {
            out.chunks.push_back(kept);
        }
    }
    return out;
}

} // namespace terrain::water

// The valley of a course at a point (river_profile.hpp).
#include "river_profile.hpp"

#include "river_lips.hpp"
#include "river_slot.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace terrain::water {

namespace {

// The headwall of a fall turns to rock over kHeadFade meters beside its channel.
constexpr double kHeadFade = 10.0;

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

// The point of segment a-b nearest to (x, z): the share t along it, the distance and how far the
// point lies beyond its ends along it (0 alongside), upstream of its start or downstream of its
// end; then (sideOf) the unit direction from it to the point and the signed distance to the line
// of the segment (left of the course positive).
struct Foot {
    double t = 0.0;
    double distance = 0.0;
    double qx = 0.0; // from it to the point
    double qz = 0.0;
    double beyond = 0.0;
    bool upstream = false;
    double side = 0.0;
    double upX = 1.0;
    double upZ = 0.0;
};

Foot footOn(const CoursePoint& a, const SegmentSpan& s, double x, double z) {
    Foot f;
    const double raw =
        s.length2 > 0.0 ? ((x - a.x) * s.dx + (z - a.z) * s.dz) / s.length2 : 0.0;
    f.t = std::clamp(raw, 0.0, 1.0);
    f.qx = x - (a.x + f.t * s.dx);
    f.qz = z - (a.z + f.t * s.dz);
    f.distance = dmath::hypot(f.qx, f.qz);
    if (s.length2 > 0.0) {
        f.beyond = std::abs(raw - f.t) * s.length;
        f.upstream = raw < 0.0;
    }
    return f;
}

void sideOf(const CoursePoint& a, const SegmentSpan& s, double x, double z, Foot& f) {
    if (f.distance > 0.0) {
        f.upX = f.qx / f.distance;
        f.upZ = f.qz / f.distance;
    } else if (s.length2 > 0.0) {
        f.upX = -s.dz / s.length;
        f.upZ = s.dx / s.length;
    }
    if (s.length2 > 0.0) {
        f.side = (s.dx * (z - a.z) - s.dz * (x - a.x)) / s.length; // left of a-b: positive
    }
}

// A segment whose water comes down more than kFaceDrop meters per meter is the face of a fall
// (the steps of river_falls: 4 to 8, the runs of a river never more than 0.12).
constexpr double kFaceDrop = 1.0;

// What bounds the surface of the valley of segment a-b at a foot: the water and the levels there,
// the walls and the floor (segmentValley goes on from them to the channel).
struct Frame {
    double t = 0.0;
    double level = 0.0;
    double ground = 0.0;
    double water = 0.0;
    double surfaceLevel = 0.0;
    double fillLevel = 0.0;
    double half = 0.0;
    double depth = 0.0;
    double amplitude = 0.0;
    double out = 0.0;
    double walls = 0.0;
    double wallSlope = 0.0;
    double floorSlope = 0.0;
    double above = 0.0;
    double floorLevel = 0.0;
    // On a stretch in steps (fall above 0), the walls of its slot (CoursePoint::slot).
    double fall = 0.0;
    double slot = 0.0;
};

Frame frameOf(const CoursePoint& a, const CoursePoint& b, const Foot& f, double rise,
              double valleyRise, double warp) {
    Frame r;
    const double t = f.t;
    r.t = t;
    r.level = lerp(a.level, b.level, t);
    r.ground = lerp(a.valley, b.valley, t);
    r.water = f.upstream ? r.level + rise * f.beyond : r.level;
    r.surfaceLevel = f.upstream ? r.ground + valleyRise * f.beyond : r.ground;
    r.fillLevel = f.upstream ? r.ground : r.ground - valleyRise * f.beyond;
    r.half = lerp(a.half, b.half, t);
    r.depth = lerp(a.depth, b.depth, t);
    const double floorHalf = lerp(a.floorHalf, b.floorHalf, t);
    const double wall = lerp(a.wall, b.wall, t);
    const double foot = std::max(1.0, lerp(a.foot, b.foot, t));
    const double d = f.distance;
    // The walls, rounded at their foot, their edge with spurs and re-entrants, but never nearer
    // to the axis than kWallMargin meters beyond the channel and its meanders (a soft bound).
    r.amplitude = lerp(a.meander, b.meander, t);
    const double least = r.half + r.amplitude + kWallMargin;
    const double spur = floorHalf - warp * wallWarpOf(floorHalf) - least;
    r.out = d - least - 0.5 * (spur + std::sqrt(spur * spur + kWallSoft * kWallSoft));
    // The walls of a gorge are steeper on its spurs and gentler in its bays (the noise of the
    // walls): no even trench.
    const double steep = wall * (1.0 + kGorgeSwing * lerp(a.gorge, b.gorge, t) * warp);
    if (r.out > 0.0) {
        r.walls =
            r.out < foot ? steep * r.out * r.out / (2.0 * foot) : steep * (r.out - 0.5 * foot);
        r.wallSlope = r.out < foot ? steep * r.out / foot : steep;
    }
    r.floorSlope = lerp(a.floorSlope, b.floorSlope, t);
    r.above = kFloorRise + r.floorSlope * std::max(0.0, d - r.half);
    r.floorLevel = r.surfaceLevel + r.above;
    r.fall = lerp(a.fall ? 1.0 : 0.0, b.fall ? 1.0 : 0.0, t);
    if (r.fall > 0.0) {
        r.slot = std::max(a.slot, b.slot);
    }
    return r;
}

// The least distance from a foot to the meandering channel of segment a-b (its line moved by at
// most kMeanderReach times the amplitude, stretched by its rate), with a margin.
double channelFrom(const CoursePoint& a, const CoursePoint& b, const Foot& f, const Frame& r) {
    const double rate =
        kMeanderRate * r.amplitude * std::abs(lerp(a.phaseRate, b.phaseRate, r.t));
    const double across =
        (std::abs(f.side) - kMeanderReach * r.amplitude) / std::sqrt(1.0 + rate * rate);
    return std::max(f.beyond, across) * (1.0 - 1e-9);
}

// A channel is far kFarMargin meters beyond what changes the valley (segmentValley): far more
// than the rounding of the distances and heights.
constexpr double kFarMargin = 1e-3;

// Whether the valley of segment a-b at a foot may change valleyAt over relief ground meters high:
// its surface below threshold (the lowest it may join the blend), its floor filling the relief,
// or its banks there. Exact: the surface is never below the floor and the walls cut down to the
// water less the depth of the channel (the channel is never lower), the fill is that of
// segmentValley, and the banks only stand within kBankReach of the channel.
bool mayMatter(const CoursePoint& a, const CoursePoint& b, const Foot& f, const Frame& r,
               double threshold, double ground) {
    const double cut = std::max(0.0, r.floorLevel - (r.water - std::max(0.0, r.depth)));
    if (!(r.floorLevel + r.walls - cut >= threshold)) {
        return true;
    }
    const double onFloor = 1.0 - smooth(0.0, kLeveeFade, r.out);
    if (std::clamp(r.fillLevel + r.above - ground, 0.0, kLevee) * onFloor > 0.0) {
        return true;
    }
    if (!(r.amplitude > 0.0) && !(r.fall > 0.0)) {
        const double edge = f.distance - r.half;
        return edge >= 0.0 && edge < kBankReach;
    }
    return !(channelFrom(a, b, f, r) - r.half >= kBankReach);
}

// The channel of segment a-b at a foot (channelOf).
struct Channel {
    double distance = 0.0;
    double half = 0.0;
    double height = 0.0;
};

// The banks of segment a-b, bank meters high out meters beyond its channel, in the slot of a fall
// drawn along meters along the course. Beyond the walls of the slot at their widest (slotReach),
// where the banks rise above the floor whatever their width there, the valley is that of the
// floor: they are only drawn nearer.
double slotBank(const Frame& r, const Foot& f, std::uint64_t slotSeed, double along, double out,
                double bank) {
    const double reach = slotReach(r.slot, r.half);
    if (out >= reach + kFarMargin &&
        lerp(bank, r.floorLevel + kBankSlope * (out - reach), r.fall) >=
            r.floorLevel + kFarMargin) {
        return r.floorLevel;
    }
    const double run = slotRun(slotSeed, along, r.slot, r.half, f.side);
    const double rise = std::max(0.0, r.floorLevel - r.water);
    const double wall = out < run ? r.water + slotWall(out, run, rise)
                                  : r.floorLevel + kBankSlope * (out - run);
    return bank + (wall - bank) * r.fall;
}

// The channel of segment a-b at a foot: how far the point lies from its meandering line, its
// half width and the height of its bed or of its banks there. On a stretch in steps, in the slot of
// the fall (river_slot.hpp): the channel narrowed, its walls rising from the water to the floor
// over their own width beyond it, whatever the depth of the steps below the floor.
Channel channelOf(const CoursePoint& a, const CoursePoint& b, const Foot& f, const Frame& r,
                  std::uint64_t seed, std::uint64_t slotSeed) {
    const double t = r.t;
    Channel c;
    c.distance = f.distance;
    c.half = r.half;
    // The slot is drawn where the point lies along the course, beyond the ends of the segment
    // too: the segments around it draw the same slot there.
    const double along = lerp(a.s, b.s, t) + (f.upstream ? -f.beyond : f.beyond);
    if (r.fall > 0.0) {
        c.half = slotHalf(slotSeed, along, r.half);
    }
    // Beyond its banks (and the rock of a headwall beside it), where they rise above the floor,
    // the channel changes nothing whatever its meanders: at the least distance to it (as
    // mayMatter's) the valley is the same. They are only drawn nearer (off the falls).
    const double nearest =
        r.amplitude > 0.0 && !(r.fall > 0.0) ? channelFrom(a, b, f, r) - c.half : 0.0;
    if (nearest >= kBankReach + kFarMargin &&
        r.floorLevel <= r.water + kBankSlope * nearest - kFarMargin) {
        c.distance = c.half + nearest;
    } else if (r.amplitude > 0.0) {
        const Meander m = meanderAt(lerp(a.phase, b.phase, t), r.amplitude, seed);
        const double rate = m.rate * lerp(a.phaseRate, b.phaseRate, t);
        // To the segment moved by the offset: continuous, even beyond its ends.
        c.distance =
            dmath::hypot(f.beyond, (f.side - m.offset) / std::sqrt(1.0 + rate * rate));
    }
    const double half = c.half;
    if (c.distance < half) {
        c.height = r.water - r.depth * (1.0 - (c.distance / half) * (c.distance / half));
        return c;
    }
    const double out = c.distance - half;
    c.height = r.water + kBankSlope * out;
    if (r.fall > 0.0) {
        c.height = slotBank(r, f, slotSeed, along, out, c.height);
    }
    return c;
}

// The rest of the valley of segment a-b at a foot from its frame. Upstream of its start, the
// surface rises rise meters per meter (risesOf: at least as fast as the level upstream of it
// comes down to it): on a steep stretch or below a fall the floor of the segments below never
// undercuts the water above them, while the segments alongside the point set its surface. The
// fill of the floor drops as fast downstream of its end, for the same reason. The valley is carved
// from its level (CoursePoint::valley, its surface rising valleyRise), the channel from the water
// (rise): on a fall, in its slot cut into the floor of the valley (channelOf).
Valley segmentValley(const CoursePoint& a, const CoursePoint& b, const Foot& f, const Frame& r,
                     std::uint64_t seed, std::uint64_t slotSeed) {
    const double t = r.t;
    const double fall = r.fall;
    const Channel ch = channelOf(a, b, f, r, seed, slotSeed);
    const double channelDistance = ch.distance;
    const double half = ch.half;
    const double channel = ch.height;
    const double cut = std::max(0.0, r.floorLevel - channel);
    // Not wet where its surface rises upstream, but up the face of a fall: there it is the face,
    // its water rising with it (where the segment above holds about the same ground).
    const bool face = a.level - b.level > kFaceDrop * (b.s - a.s);
    const double surface = face ? r.water : r.level;
    Valley v;
    v.height = r.floorLevel + r.walls - cut;
    v.level = static_cast<float>(surface);
    v.floorLevel = static_cast<float>(r.surfaceLevel);
    v.wet = channelDistance < half && channel < surface;
    v.gorge = static_cast<float>(lerp(a.gorge, b.gorge, t));
    v.mountain = static_cast<float>(lerp(a.mountain, b.mountain, t));
    v.upX = f.upX;
    v.upZ = f.upZ;
    // The slope of the valley across it, continuous (the banks of the channel aside): the rock
    // of the walls is laid along it.
    v.slope = r.floorSlope + r.wallSlope;
    // The walls above the floor, and beside the channel the rise of the surface upstream of the
    // segment and the slot of a fall below the floor: the headwall and the slot of a fall are
    // rock as the walls.
    const double beside = std::clamp((channelDistance - half) / kHeadFade, 0.0, 1.0);
    v.above = r.walls + beside * (r.surfaceLevel - r.ground +
                                  fall * std::max(0.0, r.floorLevel - r.water - kFloorRise));
    v.floor = r.fillLevel + r.above;
    v.onFloor = 1.0 - smooth(0.0, kLeveeFade, r.out);
    const double edge = channelDistance - half;
    if (edge >= 0.0 && edge < kBankReach) {
        v.bank = r.fillLevel + kFloorRise - kBankDrop * std::max(0.0, edge - kBankFlat);
    }
    return v;
}

// The blocks of the pools of the falls of c stand over the lowest valley of its segments at (x,
// z): over every segment alike, so over the lowest as well. The water stands only around them.
void standBlocks(const Course& c, double x, double z, Valley& v) {
    if (c.blocks.empty() || !(v.height < std::numeric_limits<double>::infinity())) {
        return;
    }
    const double block = blocksAt(c, x, z);
    if (block > v.height) {
        v.height = block;
        v.wet = v.wet && block < static_cast<double>(v.level);
    }
}

// The lowest valley of the segments of c around lip l at (x, z) (river_lips.hpp): those from
// kNearLip meters above where it moves the valley to as far below (the others lie higher there:
// their water upstream rising with its slot walls, or beyond their ends); neither blended nor
// filled.
constexpr double kNearLip = 4.0;
Valley valleyNear(const Course& c, const FallLip& l, double x, double z, std::uint64_t meanderSeed,
                  double warp) {
    const double from = l.s - 4.0 * static_cast<double>(l.recess + l.edge) - kNearLip;
    const double to = l.s + static_cast<double>(l.reach) + kNearLip;
    std::size_t first = l.point;
    while (first > 0 && c.points[first].s > from) {
        --first;
    }
    Valley best;
    for (std::size_t i = first; i + 1 < c.points.size() && c.points[i].s < to; ++i) {
        const CoursePoint& a = c.points[i];
        const CoursePoint& b = c.points[i + 1];
        const SegmentSpan span(a, b);
        Foot f = footOn(a, span, x, z);
        sideOf(a, span, x, z, f);
        const Frame r = frameOf(a, b, f, c.rises[i], c.valleyRises[i], warp);
        const Valley v = segmentValley(a, b, f, r, meanderSeed, c.slotSeed);
        if (v.height < best.height) {
            best = v;
        }
    }
    return best;
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

Valley valleyAt(const Course& c, double x, double z, double ground, std::uint64_t meanderSeed,
                WallWarp& warp, const BoxCourse* box) {
    Valley best;
    if (x < c.minX || x > c.maxX || z < c.minZ || z > c.maxZ) {
        return best;
    }
    const double bound = ground + kLevee;
    double lowest = bound;
    double raise = 0.0;
    double bank = -std::numeric_limits<double>::infinity();
    ValleyBlend blend;
    // Below lowest + kBlend (the lowest valley found so far, or bound) a valley may join the
    // blend; above, only its fill and its banks may matter.
    const auto take = [&](std::uint32_t i, const SegmentSpan& span, Foot& f) {
        const CoursePoint& a = c.points[i];
        const CoursePoint& b = c.points[i + 1];
        sideOf(a, span, x, z, f);
        const Frame r = frameOf(a, b, f, c.rises[i], c.valleyRises[i], warp.value());
        if (!mayMatter(a, b, f, r, lowest + kBlend, ground)) {
            return;
        }
        Valley v = segmentValley(a, b, f, r, meanderSeed, c.slotSeed);
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
    };
    // Beyond its floors (and their levees), a chunk only matters where it may carve below the
    // relief.
    const auto skips = [&](const CourseChunk& k) {
        const double away = dmath::hypot(x - k.x, z - k.z) - k.radius - k.flat;
        const double floorOf = static_cast<double>(k.minLevel - k.maxDepth) +
                               static_cast<double>(k.minWall) * std::max(0.0, away);
        return away > 0.0 && !(floorOf < lowest + kBlend);
    };
    if (box == nullptr) {
        for (const CourseChunk& k : c.chunks) {
            if (!skips(k)) {
                for (std::uint32_t i = k.first; i < k.last; ++i) {
                    const SegmentSpan span(c.points[i], c.points[i + 1]);
                    Foot f = footOn(c.points[i], span, x, z);
                    take(i, span, f);
                }
            }
        }
    } else {
        for (const BoxCourse::Chunk& k : box->chunks) {
            // Skipped over the whole box: so it is at the point (away and floor only grow).
            if ((k.away > 0.0 && !(k.floor < lowest + kBlend)) || skips(c.chunks[k.chunk])) {
                continue;
            }
            for (std::uint32_t j = k.first; j < k.last; ++j) {
                const BoxCourse::Segment& s = box->segments[j];
                Foot f = footOn(c.points[s.first], s.span, x, z);
                if (s.mayMatter(f.distance, f.beyond, f.upstream, ground, lowest + kBlend,
                                warp.value())) {
                    take(s.first, s.span, f);
                }
            }
        }
    }
    cutLips(c, x, z,
            [&](double sx, double sz, const FallLip& l) {
                return valleyNear(c, l, sx, sz, meanderSeed, warp.value());
            },
            best);
    standBlocks(c, x, z, best);
    blend.applyTo(best);
    best.raise = raise;
    best.bank = bank;
    return best;
}

Valley valleyAt(const Course& c, double x, double z, double ground, std::uint64_t meanderSeed,
                double warp) {
    WallWarp given(warp);
    return valleyAt(c, x, z, ground, meanderSeed, given);
}

} // namespace terrain::water

// The valley of a course at a point: the lowest of its segments there, blended (river_profile.hpp).
#include "river_profile.hpp"
#include "river_lips.hpp"
#include "river_segment.hpp"
#include "river_slot.hpp"
#include "river_winding.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace terrain::water {

namespace {

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// How far (x, z) lies at least from box segment s of course c, its foot f on it: in the frame of
// the slot near a corridor (river_winding.hpp).
double boxDistance(const Course& c, const BoxCourse::Segment& s, double x, double z,
                   const Foot& f) {
    if (!(s.windMost > 0.0)) {
        return f.distance;
    }
    const CoursePoint& a = c.points[s.first];
    const double side = sideAt(a, s.span, x, z);
    return windDistance(windAt(c, alongOf(a, c.points[s.first + 1], f)), f.beyond, side, side);
}

// The segments of box, in order, that may change the valley at (x, z) (those of its block):
// each(j) for them, but in the chunks skip(k) leaves out.
template <typename Skip, typename Each>
void forBoxSegments(const BoxCourse& box, double x, double z, const Skip& skip, const Each& each) {
    const std::uint32_t* end = nullptr;
    const std::uint32_t* kept = box.blockAt(x, z, end);
    for (const BoxCourse::Chunk& k : box.chunks) {
        while (kept != nullptr && kept != end && *kept < k.first) {
            ++kept;
        }
        if (skip(k)) {
            continue;
        }
        if (kept == nullptr) {
            for (std::uint32_t j = k.first; j < k.last; ++j) {
                each(j);
            }
            continue;
        }
        for (; kept != end && *kept < k.last; ++kept) {
            each(*kept);
        }
    }
}

// The lowest valley of the segments of c around lip l at (x, z) (river_lips.hpp): those from
// kNearLip meters above where it moves the valley to as far below (the others lie higher there:
// their water upstream rising with its slot walls, or beyond their ends); neither blended nor
// filled.
constexpr double kNearLip = 4.0;
Valley valleyNear(const Course& c, const FallLip& l, double x, double z, std::uint64_t meanderSeed,
                  double warp) {
    const double from = l.s - 4.0 * recessMost(l) - kNearLip;
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
        const Frame r = frameAt(c, i, span, x, z, f, warp);
        const Valley v = segmentValley(a, b, f, r, meanderSeed, c.slotSeed);
        if (v.height < best.height) {
            best = v;
        }
    }
    return best;
}

} // namespace

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
        const Frame r = frameAt(c, i, span, x, z, f, warp.value());
        const Matters matters = mayMatter(a, b, f, r, lowest + kBlend, ground);
        if (matters != Matters::Valley) {
            if (matters == Matters::Banks) { // its surface and fill change nothing: its banks
                bank = std::max(bank, bankOf(channelReach(a, b, f, r, meanderSeed), r));
            }
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
    const auto skips = [&](const CourseChunk& k) { return k.skips(x, z, lowest + kBlend); };
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
        // Skipped over the whole box: so it is at the point (away and floor only grow).
        const auto skipsBox = [&](const BoxCourse::Chunk& k) {
            return (k.away > 0.0 && !(k.floor < lowest + kBlend)) || skips(c.chunks[k.chunk]);
        };
        forBoxSegments(*box, x, z, skipsBox, [&](std::uint32_t j) {
            const BoxCourse::Segment& s = box->segments[j];
            Foot f = footOn(c.points[s.first], s.span, x, z);
            const double d = boxDistance(c, s, x, z, f);
            // Only its banks may matter: where they may stand above those of the others.
            const Matters m =
                s.mayMatter(d, f.beyond, f.upstream, ground, lowest + kBlend, warp.value());
            if (m == Matters::Valley || (m == Matters::Banks && s.bankAt(d) > bank)) {
                take(s.first, s.span, f);
            }
        });
    }
    cutLips(c, x, z,
            [&](double sx, double sz, const FallLip& l) {
                return valleyNear(c, l, sx, sz, meanderSeed, warp.value());
            },
            best, box != nullptr ? &box->lips : nullptr);
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

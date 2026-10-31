// The pools of the falls in steps and the bowls at the foot of their faces (river_slot.hpp).
#include "river_lips.hpp"
#include "river_slot.hpp"

#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace terrain::water {

namespace {

// The bowls: kBowlShare of the drop deep, from kBowlLeast meters; full at the foot of the face
// and kBowlHold of their length below it, kBowlRun times the drop long (kBowlRunLeast meters at
// least, kBowlPool of the pool at most); their bed never rising faster than kBowlSteep meters per
// meter below it (shallower where the pool is too short: between the drops of a cascade).
constexpr double kBowlShare = 0.1;
constexpr double kBowlLeast = 2.0;
constexpr double kBowlRun = 1.0;
constexpr double kBowlRunLeast = 12.0;
constexpr double kBowlPool = 0.6;
constexpr double kBowlHold = 0.3;
constexpr double kBowlSteep = 0.38;
// The channel narrows toward each lip (its sill) by kPinchLeast to kPinchMost of itself (drawn),
// over the last kPinchShare of the pool above it: the pools oval.
constexpr double kPinchLeast = 0.15;
constexpr double kPinchMost = 0.3;
constexpr double kPinchShare = 1.0 / 3.0;
constexpr std::uint64_t kPinchSalt = 0x70696e6368313241ULL;

double smoothOf(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// A share in [0, 1) from 24 bits of bits, from the bit shift.
double shareOf(std::uint64_t bits, int shift) {
    return static_cast<double>((bits >> shift) & 0xffffffULL) * 0x1.0p-24;
}

// How far the bowl of pool q reaches below the foot of its face.
double bowlLength(const FallPool& q) {
    return std::min(std::max(kBowlRun * static_cast<double>(q.drop), kBowlRunLeast),
                    kBowlPool * (q.next - q.foot));
}

// How deep the bowl of pool q is at most, length its reach.
double bowlDepth(const FallPool& q, double length) {
    // The steepest of a smooth step is 1.5 times its mean.
    return std::min(std::clamp(kBowlShare * q.drop, kBowlLeast, kBowlMost),
                    kBowlSteep * (1.0 - kBowlHold) * length / 1.5);
}

// How much the channel of pool q narrows s meters along the course: toward its lip (a sill) by
// pinch of itself, down its face and back at its foot; toward the next lip over the last
// kPinchShare of the pool, by nextPinch at that lip.
double narrowOf(const FallPool& q, double s) {
    if (s < q.foot) {
        return q.foot > q.lip ? q.pinch * (1.0 - smoothOf((s - q.lip) / (q.foot - q.lip))) : 0.0;
    }
    const double tail = kPinchShare * (q.next - q.foot);
    return tail > 0.0 && s > q.next - tail ? q.nextPinch * smoothOf((s - q.next + tail) / tail)
                                           : 0.0;
}

// The bowl of pool q s meters along the course.
Bowl bowlOf(const FallPool& q, double s) {
    const double length = q.length;
    const double narrow = narrowOf(q, s);
    if (!(length > 0.0)) {
        return {0.0, -narrow}; // no pool below the face: no bowl
    }
    // Deeper down the face, full at its foot and kBowlHold of the bowl below it; wider from the
    // lip to there (the walls of the slot giving way gradually), then both back to nothing.
    const double hold = q.foot + kBowlHold * length;
    const double back = s > hold ? 1.0 - smoothOf((s - hold) / ((1.0 - kBowlHold) * length)) : 1.0;
    const double deeper =
        s < q.foot ? (q.foot > q.lip ? smoothOf((s - q.lip) / (q.foot - q.lip)) : 1.0) : back;
    const double wider = s < hold ? smoothOf((s - q.lip) / (hold - q.lip)) : back;
    return {std::max(0.0, deeper) * q.deep, std::max(0.0, wider) * kBowlWiden - narrow};
}

// The most the bowls of c deepen and widen each of its segments, kBowlBeyond meters beyond its
// ends too (CoursePoint::bowlDeep, bowlWide): a bowl from the lip of its pool to its length below
// its foot (bowlOf), as deep as its depth there at most, rounded up.
void boundBowls(Course& c) {
    std::vector<CoursePoint>& p = c.points;
    // The segments from the one that ends at from or beyond it to the last that starts before
    // end (kBowlBeyond meters beyond them too).
    const auto mark = [&p](double from, double end, const auto& set) {
        from -= kBowlBeyond;
        end += kBowlBeyond;
        auto i = static_cast<std::size_t>(
            std::lower_bound(p.begin(), p.end(), from,
                             [](const CoursePoint& a, double s) { return a.s < s; }) -
            p.begin());
        for (i = i > 0 ? i - 1 : 0; i + 1 < p.size() && !(p[i].s > end); ++i) {
            set(p[i]);
        }
    };
    for (const FallPool& q : c.pools) {
        if (q.length > 0.0) {
            const auto deep = static_cast<float>((1.0 + 1e-6) * q.deep);
            const auto wide = static_cast<float>((1.0 + 1e-6) * kBowlWiden);
            mark(q.lip, q.foot + q.length, [&](CoursePoint& a) {
                a.bowlDeep = std::max(a.bowlDeep, deep);
                a.bowlWide = std::max(a.bowlWide, wide);
            });
        }
        const float pinch = std::max(q.pinch, q.nextPinch);
        mark(q.lip, q.foot, [&](CoursePoint& a) { a.bowlNarrow = std::max(a.bowlNarrow, pinch); });
        const double tail = kPinchShare * (q.next - q.foot);
        mark(q.next - tail, q.next,
             [&](CoursePoint& a) { a.bowlNarrow = std::max(a.bowlNarrow, pinch); });
    }
}

} // namespace

void placePools(Course& c) {
    c.pools.clear();
    const std::vector<CoursePoint>& p = c.points;
    for (std::size_t i = 0; i + 1 < p.size(); ++i) {
        if (!faceAt(p, i)) {
            continue;
        }
        std::size_t j = i + 1;
        while (j + 1 < p.size() && faceAt(p, j)) {
            ++j;
        }
        const float drop = p[i].level - p[j].level;
        if (static_cast<double>(drop) >= kLipLeast) {
            // To the next lip, or to the end of the stretch.
            std::size_t k = j;
            while (k + 1 < p.size() && p[k + 1].fall && !faceAt(p, k)) {
                ++k;
            }
            FallPool q{p[i].s, p[j].s, p[k].s, drop, 0.0, 0.0, 0.0f, 0.0f};
            q.length = bowlLength(q);
            q.deep = bowlDepth(q, q.length);
            const std::uint64_t draw =
                noise::deriveSeed(noise::deriveSeed(c.slotSeed, kPinchSalt), c.pools.size());
            q.pinch = static_cast<float>(kPinchLeast + (kPinchMost - kPinchLeast) * shareOf(draw, 0));
            if (!c.pools.empty() && c.pools.back().next == q.lip) {
                c.pools.back().nextPinch = q.pinch;
            }
            c.pools.push_back(q);
        }
        i = j - 1;
    }
    c.poolOf.assign(p.size(), -1);
    std::int32_t k = -1;
    for (std::size_t i = 0; i < p.size(); ++i) {
        while (static_cast<std::size_t>(k + 1) < c.pools.size() &&
               !(p[i].s < c.pools[static_cast<std::size_t>(k + 1)].lip)) {
            ++k;
        }
        c.poolOf[i] = k;
    }
    boundBowls(c);
}

Bowl bowlAt(const Course& c, double s) {
    const auto after = std::upper_bound(c.pools.begin(), c.pools.end(), s,
                                        [](double v, const FallPool& q) { return v < q.lip; });
    return after == c.pools.begin() ? Bowl{} : bowlOf(*(after - 1), s);
}

Bowl bowlAt(const Course& c, double s, std::size_t i) {
    const CoursePoint& a = c.points[i];
    if (!(a.bowlWide > 0.0f) && !(a.bowlNarrow > 0.0f)) {
        return {}; // no bowl near the segment (boundBowls)
    }
    s = std::clamp(s, a.s - kBowlBeyond, c.points[i + 1].s + kBowlBeyond);
    // The last pool whose lip lies at s or before it, from that of point i.
    auto k = static_cast<std::ptrdiff_t>(c.poolOf[i]);
    const auto n = static_cast<std::ptrdiff_t>(c.pools.size());
    while (k + 1 < n && !(s < c.pools[static_cast<std::size_t>(k + 1)].lip)) {
        ++k;
    }
    while (k >= 0 && s < c.pools[static_cast<std::size_t>(k)].lip) {
        --k;
    }
    return k < 0 ? Bowl{} : bowlOf(c.pools[static_cast<std::size_t>(k)], s);
}

} // namespace terrain::water

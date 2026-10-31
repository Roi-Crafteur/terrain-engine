// The steps of the falls of the small world (phase 12A): of all heights and spacings, some in
// cascades; their lips never a straight cut across their slot; a bowl at the foot of each drop; and
// heaps of blocks below them.
//   - Steps: down the corridors of the falls, the drops of their water (kDropLeast meters at least)
//     vary in height and in spacing between their lips (their coefficients of variation
//     kHeightSpread and kSpacingSpread at least, the 90th percentile of the spacing kSpacingRange
//     times its 10th at least), kCascadeShare of them in cascades (a drop within kCascadeGap
//     meters of the next one or of the one before it).
//   - Lips: across the channel at kAcross of its half width on each side and on its axis, where
//     the ground comes down through the middle of the drop: over hard rock on soft (the contrast
//     of Heightfield::rockHardness kContrast at least), the middle of the lip lies upstream of its
//     sides by kRecessLeast of the drop at least on average (an amphitheater); over rock of one
//     hardness by kStraightMost meters at most (a lip about straight), yet never as straight as a
//     ruler (its three places apart by kEdgeLeast meters at least on average). The line through
//     its sides turns from across the slot by more than kTurnLeast degrees at kTurnedShare of the
//     lips at least, kTurnMost at most.
//   - Bowls: the water of the pools kDeeperLeast meters deeper and kWiderLeast times wider just
//     below their drop than near their next lip (the medians); oval, narrower toward the next lip
//     (its sill) than at their middle, a share of them at least.
//   - Heaps: the blocks of the pools at the foot of the drops, of 1.5 to 7 meters, a share of
//     them out of the water.
#include "falls_support.hpp"
#include "pattern_support.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

using falls::Reaches;
using falls::World;
using terrain::RiverPoint;
using terrain::RiverReach;

constexpr double kRunGap = 20.0; // a corridor: its points closer than this
constexpr double kDropLeast = 2.0;
constexpr double kHeightSpread = 0.7;
constexpr double kSpacingSpread = 0.8;
constexpr double kSpacingRange = 10.0;
constexpr double kCascadeShare = 0.25;
constexpr double kCascadeGap = 10.0;
constexpr double kAcross = 0.8;
constexpr double kContrast = 0.3;
constexpr double kRecessLeast = 0.3;
constexpr std::size_t kPlaces = 9;
constexpr std::size_t kPiece = 5; // 0.4 of the width (kAcross of its half on each side)
constexpr double kOnLine = 0.02;
constexpr double kOnLineLeast = 0.3;
constexpr double kBowedLeast = 0.1;
constexpr double kBowedShare = 0.9;
constexpr double kRoomLeast = 0.35;
constexpr double kStraightShare = 0.05;
constexpr double kOpenRoom = 40.0;
constexpr double kTurnAcross = 0.6;
constexpr double kTurnLeast = 10.0;
constexpr double kTurnedShare = 0.6;
constexpr double kTurnMost = 45.0;
constexpr double kDeeperLeast = 1.5;
constexpr double kWiderLeast = 1.05;

// Meters along the points of a reach.
std::vector<double> alongOf(const RiverReach& reach) {
    const auto& p = reach.points;
    std::vector<double> s(p.size(), 0.0);
    for (std::size_t i = 1; i < p.size(); ++i) {
        s[i] = s[i - 1] + std::hypot(p[i].x - p[i - 1].x, p[i].z - p[i - 1].z);
    }
    return s;
}

// The drops of the water along a reach: from the last point of a pool (the lip) to the first
// point below its face (the foot), its height.
struct Drop {
    std::size_t lip = 0;
    std::size_t foot = 0;
    double height = 0.0;
};

std::vector<Drop> dropsOf(const RiverReach& reach) {
    const auto& p = reach.points;
    std::vector<Drop> out;
    for (std::size_t i = 0; i + 1 < p.size(); ++i) {
        if (!(p[i].level - p[i + 1].level > 0.5f) ||
            !(std::hypot(p[i + 1].x - p[i].x, p[i + 1].z - p[i].z) < kRunGap)) {
            continue;
        }
        std::size_t j = i + 1;
        while (j + 1 < p.size() && p[j].level - p[j + 1].level > 0.5f) {
            ++j;
        }
        out.push_back({i, j, static_cast<double>(p[i].level - p[j].level)});
        i = j - 1;
    }
    return out;
}

void testSteps(Reaches& reaches, const std::vector<std::size_t>& steeps) {
    std::vector<double> heights;
    std::vector<double> spacings;
    std::size_t cascades = 0;
    for (const std::size_t r : steeps) {
        const std::vector<double> s = alongOf(reaches[r]);
        std::vector<Drop> drops = dropsOf(reaches[r]);
        drops.erase(std::remove_if(drops.begin(), drops.end(),
                                   [](const Drop& d) { return d.height < kDropLeast; }),
                    drops.end());
        for (std::size_t k = 0; k < drops.size(); ++k) {
            heights.push_back(drops[k].height);
            const bool next = k + 1 < drops.size() &&
                              s[drops[k + 1].lip] - s[drops[k].foot] <= kCascadeGap;
            const bool before = k > 0 && s[drops[k].lip] - s[drops[k - 1].foot] <= kCascadeGap;
            cascades += next || before ? 1 : 0;
            if (k + 1 < drops.size() && s[drops[k + 1].lip] - s[drops[k].lip] < 1000.0) {
                spacings.push_back(s[drops[k + 1].lip] - s[drops[k].lip]);
            }
        }
    }
    const double heightSpread = pattern_test::variation(heights);
    const double spacingSpread = pattern_test::variation(spacings);
    const double share = static_cast<double>(cascades) / static_cast<double>(heights.size());
    const double range = falls::quantile(spacings, 0.9) / falls::quantile(spacings, 0.1);
    std::printf("%zu drops of the falls: heights varying by %.2f (%.1f to %.1f m from the 10th to "
                "the 90th percentile), the spacing of their lips by %.2f (%.0f to %.0f m), %.0f%% "
                "of them in cascades\n",
                heights.size(), heightSpread, falls::quantile(heights, 0.1),
                falls::quantile(heights, 0.9), spacingSpread, falls::quantile(spacings, 0.1),
                falls::quantile(spacings, 0.9), 100.0 * share);
    TEST_CHECK(heights.size() >= 200);
    TEST_CHECK(heightSpread >= kHeightSpread && spacingSpread >= kSpacingSpread);
    TEST_CHECK(range >= kSpacingRange && share >= kCascadeShare);
}

// How far upstream of lip i of a reach the water comes down through the middle of its drop,
// side meters left of the slot (searched from kSearchUp meters upstream every kSearchStep).
constexpr double kSearchUp = 30.0;
constexpr double kSearchStep = 0.1;

double edgeAt(const World& w, const RiverReach& reach, std::size_t i, double middle, double side) {
    const auto& p = reach.points;
    const double length = std::hypot(p[i + 1].x - p[i].x, p[i + 1].z - p[i].z);
    const double dx = (p[i + 1].x - p[i].x) / length;
    const double dz = (p[i + 1].z - p[i].z) / length;
    for (double t = -kSearchUp; t < 2.0 * length + 5.0; t += kSearchStep) {
        const double x = p[i].x + t * dx - side * dz;
        const double z = p[i].z + t * dz + side * dx;
        const terrain::HeightSample h = w.field.sampleAt(x, z);
        const double surface = h.water != terrain::WaterKind::None
                                   ? static_cast<double>(std::max(h.waterLevel, h.height))
                                   : static_cast<double>(h.height);
        if (surface < middle) {
            return -t;
        }
    }
    return std::numeric_limits<double>::quiet_NaN();
}

// The contrast of the rock under a lip at (x, z): the hardness of its rock under the water above
// less the softest under it (river_lips.cpp, as the generator reads it).
double contrastAt(const World& w, double x, double z, double top, double bottom) {
    const double cap = w.relief.rockHardness(x, z, top - 0.5);
    double under = 1.0;
    for (const double share : {0.5, 1.0, 1.5}) {
        under = std::min(under, static_cast<double>(
                                    w.relief.rockHardness(x, z, top - share * (top - bottom))));
    }
    return std::clamp(cap - under, 0.0, 1.0);
}

struct LipMeasures {
    std::vector<double> hollow; // the middle upstream of the sides, by the drop (contrasted)
    std::vector<double> bowed;  // the farthest from the line through its sides, by its width
    std::vector<double> turns;  // degrees from across the slot
    std::size_t straight = 0;   // with a straight piece kPiece places long
    // The same for the lips whose pools leave room (kRoomLeast of their width).
    std::vector<double> roomyBowed;
    std::size_t roomyStraight = 0;
};

void measureLip(const World& w, const RiverReach& reach, std::size_t i, bool roomy,
                LipMeasures& m) {
    const auto& p = reach.points;
    std::size_t j = i + 1;
    while (j + 1 < p.size() && p[j].level - p[j + 1].level > 0.5f) {
        ++j;
    }
    const double top = p[i].level;
    const double bottom = p[j].level;
    const double width = 0.5 * static_cast<double>(p[i].width);
    const double middle = 0.5 * (top + bottom);
    const double left = edgeAt(w, reach, i, middle, kAcross * width);
    const double centre = edgeAt(w, reach, i, middle, 0.0);
    const double right = edgeAt(w, reach, i, middle, -kAcross * width);
    const double turnLeft = edgeAt(w, reach, i, middle, kTurnAcross * width);
    const double turnRight = edgeAt(w, reach, i, middle, -kTurnAcross * width);
    if (!std::isnan(turnLeft) && !std::isnan(turnRight)) {
        m.turns.push_back(std::atan((turnLeft - turnRight) / (2.0 * kTurnAcross * width)) * 180.0 /
                          3.14159265358979323846);
    }
    if (std::isnan(left) || std::isnan(centre) || std::isnan(right)) {
        return;
    }
    const double contrast = contrastAt(w, p[i].x, p[i].z, top, bottom);
    // Its middle against the line through its sides.
    const double recess = centre - 0.5 * (left + right);
    if (contrast >= kContrast) {
        m.hollow.push_back(recess / (top - bottom));
    }
    // Its edge at kPlaces places across: how far from the line through its sides, and whether
    // any kPiece consecutive ones lie on a line (within kOnLine of the width).
    std::array<double, kPlaces> edge{};
    for (std::size_t k = 0; k < kPlaces; ++k) {
        const double u = -kAcross + 2.0 * kAcross * static_cast<double>(k) / (kPlaces - 1.0);
        edge[k] = edgeAt(w, reach, i, middle, u * width);
        if (std::isnan(edge[k])) {
            return;
        }
    }
    const auto off = [&](std::size_t a, std::size_t b, std::size_t k) {
        const double t = static_cast<double>(k - a) / static_cast<double>(b - a);
        return std::abs(edge[k] - (edge[a] + t * (edge[b] - edge[a])));
    };
    double bowed = 0.0;
    for (std::size_t k = 1; k + 1 < kPlaces; ++k) {
        bowed = std::max(bowed, off(0, kPlaces - 1, k));
    }
    m.bowed.push_back(bowed / (2.0 * width));
    if (roomy) {
        m.roomyBowed.push_back(bowed / (2.0 * width));
    }
    bool straight = false;
    for (std::size_t a = 0; a + kPiece <= kPlaces; ++a) {
        double most = 0.0;
        for (std::size_t k = a + 1; k + 1 < a + kPiece; ++k) {
            most = std::max(most, off(a, a + kPiece - 1, k));
        }
        straight = straight || most < std::max(kOnLineLeast, kOnLine * 2.0 * width);
    }
    m.straight += straight ? 1 : 0;
    m.roomyStraight += straight && roomy ? 1 : 0;
}

double meanOf(const std::vector<double>& values) {
    double mean = 0.0;
    for (const double v : values) {
        mean += v / static_cast<double>(std::max<std::size_t>(1, values.size()));
    }
    return mean;
}

void testLips(const World& w, Reaches& reaches, const std::vector<std::size_t>& steeps) {
    LipMeasures m;
    for (const std::size_t r : steeps) {
        const RiverReach& reach = reaches[r];
        const auto& p = reach.points;
        const std::vector<double> s = alongOf(reach);
        const std::vector<Drop> drops = dropsOf(reach);
        for (std::size_t k = 0; k < drops.size(); ++k) {
            const std::size_t i = drops[k].lip;
            const double run = std::hypot(p[i + 1].x - p[i].x, p[i + 1].z - p[i].z);
            if (i == 0 || i + 2 >= p.size() || !(p[i - 1].level == p[i].level) ||
                !(p[i].level - p[i + 1].level > 2.0f) ||
                !(static_cast<double>(p[i].level - p[i + 1].level) > run)) {
                continue;
            }
            // The room of its pools: half of the pool above, half of the pool below.
            const double above = k > 0 ? s[i] - s[drops[k - 1].foot] : kOpenRoom;
            const double below = k + 1 < drops.size() ? s[drops[k + 1].lip] - s[drops[k].foot]
                                                      : kOpenRoom;
            const bool roomy = 0.5 * std::min(above, below) >=
                               kRoomLeast * static_cast<double>(p[i].width);
            measureLip(w, reach, i, roomy, m);
        }
    }
    const auto shareBowed = [](const std::vector<double>& bows) {
        std::size_t bowed = 0;
        for (const double b : bows) {
            bowed += b >= kBowedLeast ? 1 : 0;
        }
        return static_cast<double>(bowed) / static_cast<double>(std::max<std::size_t>(1, bows.size()));
    };
    const double bowedShare = shareBowed(m.bowed);
    const double roomyShare = shareBowed(m.roomyBowed);
    std::printf("of them %zu whose pools leave room: %.0f%% bowed, %zu with a straight piece\n",
                m.roomyBowed.size(), 100.0 * roomyShare, m.roomyStraight);
    std::size_t turned = 0;
    double most = 0.0;
    for (const double t : m.turns) {
        turned += std::abs(t) > kTurnLeast ? 1 : 0;
        most = std::max(most, std::abs(t));
    }
    const double share = static_cast<double>(turned) / static_cast<double>(m.turns.size());
    std::printf("%zu lips over hard rock on soft: their middle %.2f of their drop upstream of "
                "their sides on average; %zu lips at %zu places: %.0f%% of them %.2f of their "
                "width off the line through their sides at least (median %.2f), %zu with a "
                "straight piece; %zu lips: %.0f%% of them turned from across their slot by more "
                "than %.0f degrees, %.1f at most\n",
                m.hollow.size(), meanOf(m.hollow), m.bowed.size(), kPlaces, 100.0 * bowedShare,
                kBowedLeast, falls::quantile(m.bowed, 0.5), m.straight, m.turns.size(),
                100.0 * share, kTurnLeast, most);
    TEST_CHECK(m.hollow.size() >= 5 && m.bowed.size() >= 100 && m.turns.size() >= 100);
    TEST_CHECK(meanOf(m.hollow) >= kRecessLeast);
    TEST_CHECK(m.roomyBowed.size() >= 50 && roomyShare >= kBowedShare);
    TEST_CHECK(static_cast<double>(m.roomyStraight) <=
               kStraightShare * static_cast<double>(m.roomyBowed.size()));
    TEST_CHECK(share >= kTurnedShare && most <= kTurnMost);
}

// The water of a reach at along meters along its points, between points first and last: its
// depth on the middle and how far it reaches across (meters, both sides).
std::array<double, 2> waterAt(const World& w, const RiverReach& reach, const std::vector<double>& s,
                              std::size_t first, std::size_t last, double along) {
    const auto& p = reach.points;
    std::size_t q = first;
    while (q + 1 < last && s[q + 1] < along) {
        ++q;
    }
    const double length = s[q + 1] - s[q];
    const double t = std::clamp((along - s[q]) / length, 0.0, 1.0);
    const double x = p[q].x + t * (p[q + 1].x - p[q].x);
    const double z = p[q].z + t * (p[q + 1].z - p[q].z);
    const double nx = -(p[q + 1].z - p[q].z) / length;
    const double nz = (p[q + 1].x - p[q].x) / length;
    const terrain::HeightSample middle = w.field.sampleAt(x, z);
    double across = 0.0;
    for (const double side : {-1.0, 1.0}) {
        for (double d = 0.0; d < 3.0 * static_cast<double>(p[q].width); d += 0.5) {
            const double ax = x + side * d * nx;
            const double az = z + side * d * nz;
            if (w.field.sampleAt(ax, az).water == terrain::WaterKind::None) {
                across += d;
                break;
            }
        }
    }
    return {static_cast<double>(middle.waterLevel - middle.height), across};
}

// The pools below drops of kBowlDrop meters at least, kBowlPool meters long at least: the water
// kBowlBelow meters below the foot against kBowlTail of the way to the next drop.
constexpr double kBowlDrop = 3.0;
constexpr double kBowlPool = 25.0;
constexpr double kBowlBelow = 3.0;
constexpr double kBowlTail = 0.85;
// Oval: before a drop of kSillDrop meters at least, the water at its narrowest from kSillFrom to
// kSillTo of the way to it (every kSillStep: its sill, wherever the edge of the lip recedes) as
// wide as kOvalMost of it at the middle of the pool at most, in kOvalShare of the pools at least
// (the edge of a lip receding far into its pool takes its sill away).
constexpr double kSillDrop = 4.0;
constexpr double kSillFrom = 0.8;
constexpr double kSillTo = 0.97;
constexpr double kSillStep = 0.01;
constexpr double kOvalMost = 0.85;
constexpr double kOvalShare = 0.1;

void testBowls(const World& w, Reaches& reaches, const std::vector<std::size_t>& steeps) {
    std::vector<double> deeper;
    std::vector<double> wider;
    std::vector<double> oval;
    for (const std::size_t r : steeps) {
        const RiverReach& reach = reaches[r];
        const std::vector<double> s = alongOf(reach);
        const std::vector<Drop> drops = dropsOf(reach);
        for (std::size_t k = 0; k + 1 < drops.size(); ++k) {
            const std::size_t foot = drops[k].foot;
            const std::size_t next = drops[k + 1].lip;
            const double pool = s[next] - s[foot];
            if (drops[k].height < kBowlDrop || pool < kBowlPool || pool > 1000.0) {
                continue;
            }
            const auto below = waterAt(w, reach, s, foot, next, s[foot] + kBowlBelow);
            const auto tail = waterAt(w, reach, s, foot, next, s[foot] + kBowlTail * pool);
            deeper.push_back(below[0] - tail[0]);
            wider.push_back(below[1] / std::max(1.0, tail[1]));
            // Oval: its water at its sill against the middle of the pool.
            if (drops[k + 1].height >= kSillDrop) {
                double sill = std::numeric_limits<double>::infinity();
                for (double f = kSillFrom; f <= kSillTo; f += kSillStep) {
                    const double across = waterAt(w, reach, s, foot, next, s[foot] + f * pool)[1];
                    sill = across > 0.0 ? std::min(sill, across) : sill;
                }
                const auto middle = waterAt(w, reach, s, foot, next, s[foot] + 0.5 * pool);
                oval.push_back(sill / std::max(1.0, middle[1]));
            }
        }
    }
    std::printf("%zu pools before a sill: their water %.2f, %.2f and %.2f as wide there as at "
                "their middle (the 10th, 25th and 50th percentiles)\n",
                oval.size(), falls::quantile(oval, 0.1), falls::quantile(oval, 0.25),
                falls::quantile(oval, 0.5));
    TEST_CHECK(oval.size() >= 30 && falls::quantile(oval, kOvalShare) <= kOvalMost);
    const double deep = falls::quantile(deeper, 0.5);
    const double wide = falls::quantile(wider, 0.5);
    std::printf("%zu pools below the drops: the water %.2f m deeper (median, %.2f at the 10th "
                "percentile) and %.2f times as wide (median) at their foot as near their end\n",
                deeper.size(), deep, falls::quantile(deeper, 0.1), wide);
    TEST_CHECK(deeper.size() >= 50);
    TEST_CHECK(deep >= kDeeperLeast && wide >= kWiderLeast);
}

// The blocks of the pools: in heaps at the foot of the drops (kHeapNear meters below the nearest
// foot of a drop upstream for the median block), of kBlockLeast to kBlockMost meters (their 90th
// percentile kBlockSpread times their 10th at least), a share of them kOutLeast to kOutMost out
// of the water of their pool, by kOutHigh meters at most.
constexpr double kHeapNear = 25.0;
constexpr double kBlockLeast = 1.5;
constexpr double kBlockMost = 7.0;
constexpr double kBlockSpread = 1.5;
constexpr double kOutLeast = 0.2;
constexpr double kOutMost = 0.6;
constexpr double kOutHigh = 4.0;

void testHeaps(Reaches& reaches, const std::vector<std::size_t>& steeps) {
    std::vector<double> widths;
    std::vector<double> below; // meters below the nearest foot upstream
    std::vector<double> out;   // meters out of the water, of those out of it
    for (const std::size_t r : steeps) {
        const RiverReach& reach = reaches[r];
        const auto& p = reach.points;
        const std::vector<double> s = alongOf(reach);
        for (const terrain::RiverBlock& b : reach.blocks) {
            widths.push_back(2.0 * static_cast<double>(b.radius));
            std::size_t near = 0;
            for (std::size_t i = 1; i < p.size(); ++i) {
                if (std::hypot(p[i].x - b.x, p[i].z - b.z) <
                    std::hypot(p[near].x - b.x, p[near].z - b.z)) {
                    near = i;
                }
            }
            std::size_t foot = near;
            while (foot > 0 && !(p[foot - 1].level - p[foot].level > 2.0f)) {
                --foot;
            }
            below.push_back(s[near] - s[foot]);
            const double water = p[near].level;
            if (static_cast<double>(b.top) > water) {
                out.push_back(static_cast<double>(b.top) - water);
            }
        }
    }
    const double share = static_cast<double>(out.size()) /
                         static_cast<double>(std::max<std::size_t>(1, widths.size()));
    const double least = falls::quantile(widths, 0.0);
    const double most = falls::quantile(widths, 1.0);
    const double spread =
        falls::quantile(widths, 0.9) / std::max(1e-9, falls::quantile(widths, 0.1));
    const double median = falls::quantile(below, 0.5);
    const double highest = falls::quantile(out, 1.0);
    std::printf("%zu blocks: %.1f to %.1f m wide (their 90th percentile %.1f times their 10th), "
                "%.0f m below the foot of a drop (median), %.0f%% out of the water, by %.1f m "
                "(median) to %.1f m\n",
                widths.size(), least, most, spread, median, 100.0 * share,
                falls::quantile(out, 0.5), highest);
    TEST_CHECK(widths.size() >= 100);
    TEST_CHECK(least >= kBlockLeast - 1e-3 && most <= kBlockMost + 1e-3 && spread >= kBlockSpread);
    TEST_CHECK(median <= kHeapNear);
    TEST_CHECK(share >= kOutLeast && share <= kOutMost && highest <= kOutHigh + 1e-3);
}

} // namespace

int main() {
    const World w = falls::makeWorld();
    Reaches reaches(*w.hydrology);
    const std::vector<std::size_t> steeps = falls::steepReaches(w, reaches);
    testSteps(reaches, steeps);
    testLips(w, reaches, steeps);
    testBowls(w, reaches, steeps);
    testHeaps(reaches, steeps);
    return test::result();
}

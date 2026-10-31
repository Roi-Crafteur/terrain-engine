// The steps of the falls of the small world (phase 12A): of all heights and spacings, never a box
// of short pools, some rapids; a bowl at the foot of each drop; and heaps of blocks below them
// (their lips: river_lips_test).
//   - Steps: down the corridors of the falls, the drops of their water (kDropLeast meters at least)
//     vary in height and in spacing between their lips (their coefficients of variation
//     kHeightSpread and kSpacingSpread at least, the 90th percentile of the spacing kSpacingRange
//     times its 10th at least); no pool between two drops shorter than kPoolLeast meters, never
//     kShortRun pools shorter than kShortPool meters in a row (rectangles seen from above).
//   - Rapids: runs of the corridors kRampLength meters long at least whose water comes down more
//     than kRampSlope and less than kFace meters per meter: kRapidsLeast of them at least, a block
//     per kRampBlocks meters of them at least (all of them together), their water never stepping
//     along their axis (kRampJump meters at most between samples kEndStep apart, off their blocks
//     and kRampEnd meters off their ends, where the lip of a drop below may recede).
//   - Bowls: the water of the pools kDeeperLeast meters deeper and kWiderLeast times wider just
//     below their drop than near their next lip (the medians); oval, narrower toward the next lip
//     (its sill) than at their middle, a share of them at least.
//   - Heaps: the blocks of the pools at the foot of the drops, of 1.5 to 7 meters, a share of
//     them out of the water.
#include "falls_support.hpp"
#include "pattern_support.hpp"
#include "steps_support.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numbers>
#include <vector>

namespace {

using falls::Reaches;
using falls::World;
using steps::alongOf;
using steps::Drop;
using steps::dropsOf;
using steps::kFace;
using steps::kRunGap;
using terrain::RiverPoint;
using terrain::RiverReach;

constexpr double kDropLeast = 2.0;
constexpr double kHeightSpread = 0.7;
constexpr double kSpacingSpread = 0.8;
constexpr double kSpacingRange = 8.0;
constexpr double kPoolLeast = 4.0;
constexpr double kShortPool = 20.0;
constexpr std::size_t kShortRun = 3;
constexpr double kRampLength = 6.0;
constexpr double kRampSlope = 0.15;
constexpr std::size_t kRapidsLeast = 10;
constexpr double kRampBlocks = 15.0;
constexpr double kRampJump = 0.5;
constexpr double kRampEnd = 5.0;
constexpr double kDeeperLeast = 1.5;
constexpr double kWiderLeast = 1.05;

void testSteps(Reaches& reaches, const std::vector<std::size_t>& steeps) {
    std::vector<double> heights;
    std::vector<double> spacings;
    std::vector<double> pools;
    std::size_t shortRun = 0;
    for (const std::size_t r : steeps) {
        const std::vector<double> s = alongOf(reaches[r]);
        std::vector<Drop> drops = dropsOf(reaches[r]);
        drops.erase(std::remove_if(drops.begin(), drops.end(),
                                   [](const Drop& d) { return d.height < kDropLeast; }),
                    drops.end());
        std::size_t run = 0;
        for (std::size_t k = 0; k < drops.size(); ++k) {
            heights.push_back(drops[k].height);
            if (k + 1 < drops.size() && s[drops[k + 1].lip] - s[drops[k].lip] < 1000.0) {
                spacings.push_back(s[drops[k + 1].lip] - s[drops[k].lip]);
                pools.push_back(s[drops[k + 1].lip] - s[drops[k].foot]);
                run = pools.back() < kShortPool ? run + 1 : 0;
                shortRun = std::max(shortRun, run);
            }
        }
    }
    const double heightSpread = pattern_test::variation(heights);
    const double spacingSpread = pattern_test::variation(spacings);
    const double range = falls::quantile(spacings, 0.9) / falls::quantile(spacings, 0.1);
    std::printf("%zu drops of the falls: heights varying by %.2f (%.1f to %.1f m from the 10th to "
                "the 90th percentile), the spacing of their lips by %.2f (%.0f to %.0f m); their "
                "pools %.1f m long at least, %zu shorter than %.0f m in a row at most\n",
                heights.size(), heightSpread, falls::quantile(heights, 0.1),
                falls::quantile(heights, 0.9), spacingSpread, falls::quantile(spacings, 0.1),
                falls::quantile(spacings, 0.9), falls::quantile(pools, 0.0), shortRun, kShortPool);
    TEST_CHECK(heights.size() >= 200);
    TEST_CHECK(heightSpread >= kHeightSpread && spacingSpread >= kSpacingSpread);
    TEST_CHECK(range >= kSpacingRange);
    TEST_CHECK(falls::quantile(pools, 0.0) >= kPoolLeast && shortRun < kShortRun);
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
// of the water around them (that of the field, the mean of its wet samples kRing times their
// radius from their middle: a pool, or rapids), by kOutHigh meters at most.
constexpr double kHeapNear = 25.0;
constexpr double kBlockLeast = 1.5;
constexpr double kBlockMost = 7.0;
constexpr double kBlockSpread = 1.5;
constexpr double kOutLeast = 0.2;
constexpr double kOutMost = 0.6;
constexpr double kOutHigh = 4.0;
constexpr double kRing = 1.5;
constexpr int kRingSamples = 16;

// The water of the field around block b (kRing), or none.
double waterAround(const World& w, const terrain::RiverBlock& b, double none) {
    double sum = 0.0;
    int wet = 0;
    for (int n = 0; n < kRingSamples; ++n) {
        const double a = 2.0 * std::numbers::pi * n / kRingSamples;
        const double r = kRing * static_cast<double>(b.radius);
        const terrain::HeightSample h =
            w.field.sampleAt(b.x + r * std::cos(a), b.z + r * std::sin(a));
        if (h.water != terrain::WaterKind::None) {
            sum += static_cast<double>(h.waterLevel);
            ++wet;
        }
    }
    return wet > 0 ? sum / wet : none;
}

void testHeaps(const World& w, Reaches& reaches, const std::vector<std::size_t>& steeps) {
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
            const double water = waterAround(w, b, p[near].level);
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

// The rapids of a reach: its runs of points (corridors) whose water comes down more than
// kRampSlope and less than kFace meters per meter, kRampLength meters long at least.
struct Rapids {
    std::size_t first = 0;
    std::size_t last = 0;
};

std::vector<Rapids> rapidsOf(const RiverReach& reach, const std::vector<double>& s) {
    const auto& p = reach.points;
    const auto sloped = [&](std::size_t i) {
        const double run = s[i + 1] - s[i];
        const double drop = static_cast<double>(p[i].level - p[i + 1].level);
        return run > 0.0 && run < kRunGap && drop > kRampSlope * run && !(drop > kFace * run);
    };
    std::vector<Rapids> out;
    for (std::size_t i = 0; i + 1 < p.size(); ++i) {
        if (!sloped(i)) {
            continue;
        }
        std::size_t j = i + 1;
        while (j + 1 < p.size() && sloped(j)) {
            ++j;
        }
        if (s[j] - s[i] >= kRampLength) {
            out.push_back({i, j});
        }
        i = j - 1;
    }
    return out;
}

void testRapids(const World& w, Reaches& reaches, const std::vector<std::size_t>& steeps) {
    std::size_t rapids = 0;
    std::size_t blocks = 0;
    double length = 0.0;
    double jump = 0.0;
    for (const std::size_t r : steeps) {
        const RiverReach& reach = reaches[r];
        const std::vector<double> s = alongOf(reach);
        const auto& p = reach.points;
        for (const Rapids& run : rapidsOf(reach, s)) {
            ++rapids;
            length += s[run.last] - s[run.first];
            const auto onBlock = [&](double x, double z, double margin) {
                return std::any_of(reach.blocks.begin(), reach.blocks.end(),
                                   [&](const terrain::RiverBlock& b) {
                                       return std::hypot(b.x - x, b.z - z) <
                                              static_cast<double>(b.radius) + margin;
                                   });
            };
            for (const terrain::RiverBlock& b : reach.blocks) {
                const auto near = [&](const RiverPoint& q) {
                    return std::hypot(q.x - b.x, q.z - b.z) < 0.5 * static_cast<double>(q.width);
                };
                blocks += std::any_of(p.begin() + static_cast<std::ptrdiff_t>(run.first),
                                      p.begin() + static_cast<std::ptrdiff_t>(run.last) + 1, near)
                              ? 1
                              : 0;
            }
            // Off their ends: the lip of a drop below them recedes into them (river_lips.hpp).
            double before = std::numeric_limits<double>::quiet_NaN();
            for (double along = s[run.first] + kRampEnd; along <= s[run.last] - kRampEnd;
                 along += falls::kEndStep) {
                std::size_t i = run.first;
                while (i + 1 < run.last && s[i + 1] < along) {
                    ++i;
                }
                const double t = std::clamp((along - s[i]) / (s[i + 1] - s[i]), 0.0, 1.0);
                const double x = p[i].x + t * (p[i + 1].x - p[i].x);
                const double z = p[i].z + t * (p[i + 1].z - p[i].z);
                const terrain::HeightSample h = w.field.sampleAt(x, z);
                if (h.water == terrain::WaterKind::None || onBlock(x, z, 0.5)) {
                    before = std::numeric_limits<double>::quiet_NaN();
                    continue;
                }
                const double level = static_cast<double>(h.waterLevel);
                jump = std::isnan(before) ? jump : std::max(jump, std::abs(level - before));
                before = level;
            }
        }
    }
    std::printf("%zu rapids of the falls, %.0f m in all: %zu blocks on them (one per %.1f m), "
                "their water stepping %.2f m at most every %.1f m along them\n",
                rapids, length, blocks, length / std::max<double>(1.0, static_cast<double>(blocks)),
                jump, falls::kEndStep);
    TEST_CHECK(rapids >= kRapidsLeast);
    TEST_CHECK(static_cast<double>(blocks) * kRampBlocks >= length);
    TEST_CHECK(jump <= kRampJump);
}

int main() {
    const World w = falls::makeWorld();
    Reaches reaches(*w.hydrology);
    const std::vector<std::size_t> steeps = falls::steepReaches(w, reaches);
    testSteps(reaches, steeps);
    testBowls(w, reaches, steeps);
    testHeaps(w, reaches, steeps);
    testRapids(w, reaches, steeps);
    return test::result();
}

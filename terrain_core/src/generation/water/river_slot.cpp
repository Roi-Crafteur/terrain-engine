// The slot of a fall in steps and the blocks of its pools (river_slot.hpp).
#include "river_slot.hpp"

#include "river_profile.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace terrain::water {

namespace {

// The walls of a slot reach kSlotShare of the mean drop of its steps beyond its channel, from
// kSlotLeast to kSlotMost meters.
constexpr double kSlotShare = 0.4;
constexpr double kSlotLeast = 4.0;
constexpr double kSlotMost = 25.0;
// Their width swings by kSlotSwing of it over waves of kSlotWave meters along the course, each
// side its own, their edges broken by kEdgeSwing of it over kEdgeWave meters; the channel narrows
// by up to kNarrowMost of it over kNarrowWave meters; the axis of the ravine wanders about the
// channel by kSlotWander of the width of its walls and of its channel (the amplitude of
// meanderAt) over kWanderWave meters.
constexpr double kSlotWave = 40.0;
constexpr double kSlotSwing = 0.45;
constexpr double kEdgeWave = 7.0;
constexpr double kEdgeSwing = 0.15;
constexpr double kNarrowWave = 30.0;
constexpr double kNarrowMost = 0.2;
constexpr double kWanderWave = 55.0;
constexpr double kSlotWander = 0.25;
// The walls never reach less than kRunLeast of their width beyond the channel; a ledge along the
// water rising kLedgeRise meters per meter, then a cliff over the last kCliffShare of them.
constexpr double kRunLeast = 0.25;
constexpr double kLedgeRise = 0.05;
constexpr double kCliffShare = 0.3;
// The spurs of rock: in kSpurChance of the cells of kSpurCell meters along each side, one, from
// kSpurLeast to kSpurMost meters long on each side of its middle, jutting out by kSpurShare to
// kSpurShare + kSpurSpread of the walls there.
constexpr double kSpurCell = 30.0;
constexpr double kSpurChance = 0.8;
constexpr double kSpurLeast = 3.0;
constexpr double kSpurMost = 8.0;
constexpr double kSpurShare = 0.25;
constexpr double kSpurSpread = 0.2;
// The blocks: in the pools of kPoolLeast meters at least, kBlockEnds meters at least from the ends
// of the course (another reach carries on there), about one every kBlockSpacing meters, in
// channels kBlockHalfLeast meters wide at least on each side of their axis.
constexpr double kPoolLeast = 8.0;
constexpr double kBlockEnds = 30.0;
constexpr double kBlockSpacing = 18.0;
constexpr double kBlockHalfLeast = 2.5;
constexpr double kBlockLeast = 2.0;
constexpr double kBlockMost = 5.0;
constexpr double kBlockSunk = 1.0;
constexpr double kBlockOut = 2.5;
constexpr double kBlockLip = 0.5;
constexpr double kBlockBelow = 2.0; // their foot below the bed
constexpr double kBlockAbove = 0.5; // their top above it at least

constexpr std::uint64_t kWidthLeft = 1;
constexpr std::uint64_t kWidthRight = 2;
constexpr std::uint64_t kEdgeLeft = 3;
constexpr std::uint64_t kEdgeRight = 4;
constexpr std::uint64_t kNarrow = 5;
constexpr std::uint64_t kWander = 6;
constexpr std::uint64_t kSpurLeft = 7;
constexpr std::uint64_t kSpurRight = 8;
constexpr std::uint64_t kBlocks = 9;

double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

// A share in [0, 1) from 24 bits of bits, from the bit shift.
double shareOf(std::uint64_t bits, int shift) {
    return static_cast<double>((bits >> shift) & 0xffffffULL) * 0x1.0p-24;
}

// A gradient noise in [-1, 1] of s over waves of wave meters.
double noiseOf(std::uint64_t seed, std::uint64_t stream, double s, double wave) {
    return std::clamp(meanderAt(s / wave, 0.5, noise::deriveSeed(seed, stream)).offset, -1.0, 1.0);
}

// How much of the walls the spurs of a side take at s (0: none).
double spurAt(std::uint64_t seed, double s) {
    const auto cell = static_cast<std::int64_t>(std::floor(s / kSpurCell));
    double spur = 0.0;
    for (std::int64_t k = cell - 1; k <= cell + 1; ++k) {
        const std::uint64_t bits = noise::deriveSeed(seed, static_cast<std::uint64_t>(k));
        if (!(shareOf(bits, 0) < kSpurChance)) {
            continue;
        }
        const double middle = (static_cast<double>(k) + shareOf(bits, 24)) * kSpurCell;
        const double length = kSpurLeast + (kSpurMost - kSpurLeast) * shareOf(bits, 40);
        const double d = std::abs(s - middle) / length;
        if (d < 1.0) {
            const double share =
                kSpurShare + kSpurSpread * shareOf(noise::deriveSeed(bits, 1), 0);
            spur = std::max(spur, share * (1.0 - d * d));
        }
    }
    return spur;
}

// The blocks of the pool of points i to j of c (at the level of i), the k-th block of the course
// next.
void placePool(Course& c, std::size_t i, std::size_t j, std::uint64_t& k) {
    const std::vector<CoursePoint>& p = c.points;
    const double s0 = p[i].s;
    const double length = p[j].s - s0;
    if (!(length >= kPoolLeast)) {
        return;
    }
    const double level = p[i].level;
    const double cap = (i > 0 ? static_cast<double>(p[i - 1].level) : level) - kBlockLip;
    const std::uint64_t seed = noise::deriveSeed(c.slotSeed, kBlocks);
    const double count =
        std::floor(length / kBlockSpacing + shareOf(noise::deriveSeed(seed, k), 0));
    for (int n = 0; n < static_cast<int>(count); ++n) {
        const std::uint64_t bits = noise::deriveSeed(seed, ++k);
        const double s = s0 + 2.0 + (length - 6.0) * shareOf(bits, 0);
        if (s < p.front().s + kBlockEnds || s > p.back().s - kBlockEnds) {
            continue;
        }
        std::size_t q = i;
        while (q + 1 < j && p[q + 1].s < s) {
            ++q;
        }
        const CoursePoint& a = p[q];
        const CoursePoint& b = p[q + 1];
        const double run = b.s - a.s;
        const double t = run > 0.0 ? std::clamp((s - a.s) / run, 0.0, 1.0) : 0.0;
        const double dx = b.x - a.x;
        const double dz = b.z - a.z;
        const double along = dmath::hypot(dx, dz);
        const double half = slotHalf(c.slotSeed, s, lerp(a.half, b.half, t));
        if (!(along > 0.0) || half < kBlockHalfLeast) {
            continue;
        }
        const double radius =
            std::min(0.5 * (kBlockLeast + (kBlockMost - kBlockLeast) * shareOf(bits, 24)),
                     0.35 * half);
        const double aside = (2.0 * shareOf(bits, 40) - 1.0) * 0.8 * (half - radius);
        // The bed of the channel there (river_profile.cpp): the block stands above it.
        const double off = aside / half;
        const double bed =
            level - static_cast<double>(lerp(a.depth, b.depth, t)) * (1.0 - off * off);
        const double top = std::min(
            std::max(level - kBlockSunk +
                         (kBlockSunk + kBlockOut) * shareOf(noise::deriveSeed(bits, 1), 0),
                     bed + kBlockAbove),
            cap);
        const double base = bed - kBlockBelow;
        if (!(top > base + 0.5)) {
            continue;
        }
        c.blocks.push_back({lerp(a.x, b.x, t) - aside * dz / along,
                            lerp(a.z, b.z, t) + aside * dx / along, static_cast<float>(radius),
                            static_cast<float>(top), static_cast<float>(base)});
    }
}

} // namespace

float slotBase(double mean) {
    return static_cast<float>(std::clamp(kSlotShare * mean, kSlotLeast, kSlotMost));
}

double slotHalf(std::uint64_t seed, double s, double half) {
    return half * (1.0 - kNarrowMost * 0.5 * (1.0 + noiseOf(seed, kNarrow, s, kNarrowWave)));
}

double slotRun(std::uint64_t seed, double s, double base, double half, double side) {
    const bool left = side >= 0.0;
    const double width = base * (1.0 + kSlotSwing * noiseOf(seed, left ? kWidthLeft : kWidthRight,
                                                            s, kSlotWave) +
                                 kEdgeSwing * noiseOf(seed, left ? kEdgeLeft : kEdgeRight, s,
                                                      kEdgeWave));
    const double spur = spurAt(noise::deriveSeed(seed, left ? kSpurLeft : kSpurRight), s);
    // The ravine wanders about its channel: one wall nearer as the other recedes.
    const double wander =
        meanderAt(s / kWanderWave, kSlotWander * (base + half), noise::deriveSeed(seed, kWander))
            .offset;
    return std::max(width * (1.0 - spur) + (left ? wander : -wander), kRunLeast * base);
}

double slotReach(double base, double half) {
    // The noises within [-1, 1], the spurs only narrowing it, the wander within its amplitude
    // (meanderAt).
    return (1.0 + kSlotSwing + kEdgeSwing) * base + kSlotWander * (base + half);
}

double slotWall(double out, double run, double rise) {
    const double ledge = (1.0 - kCliffShare) * run;
    const double low = std::min(rise, kLedgeRise * std::min(out, ledge));
    if (out < ledge) {
        return low;
    }
    return low + (rise - low) * std::min(1.0, (out - ledge) / (run - ledge));
}

void placeBlocks(Course& c) {
    c.blocks.clear();
    const std::vector<CoursePoint>& p = c.points;
    std::uint64_t k = 0;
    for (std::size_t i = 0; i + 1 < p.size();) {
        if (!(p[i].fall && p[i + 1].fall && p[i].level == p[i + 1].level)) {
            ++i;
            continue;
        }
        std::size_t j = i + 1;
        while (j + 1 < p.size() && p[j + 1].fall && p[j + 1].level == p[i].level) {
            ++j;
        }
        placePool(c, i, j, k);
        i = j;
    }
    c.blocksMinX = c.blocksMinZ = std::numeric_limits<double>::infinity();
    c.blocksMaxX = c.blocksMaxZ = -std::numeric_limits<double>::infinity();
    for (const FallBlock& b : c.blocks) {
        const double r = b.radius;
        c.blocksMinX = std::min(c.blocksMinX, b.x - r);
        c.blocksMinZ = std::min(c.blocksMinZ, b.z - r);
        c.blocksMaxX = std::max(c.blocksMaxX, b.x + r);
        c.blocksMaxZ = std::max(c.blocksMaxZ, b.z + r);
    }
}

double blocksAt(const Course& c, double x, double z) {
    double top = -std::numeric_limits<double>::infinity();
    if (c.blocks.empty() || x < c.blocksMinX || x > c.blocksMaxX || z < c.blocksMinZ ||
        z > c.blocksMaxZ) {
        return top;
    }
    for (const FallBlock& b : c.blocks) {
        const double dx = x - b.x;
        const double dz = z - b.z;
        const double r2 = static_cast<double>(b.radius) * b.radius;
        const double d2 = dx * dx + dz * dz;
        if (d2 < r2) {
            const double rise = static_cast<double>(b.top) - b.base;
            top = std::max(top, static_cast<double>(b.top) - rise * d2 / r2);
        }
    }
    return top;
}

} // namespace terrain::water

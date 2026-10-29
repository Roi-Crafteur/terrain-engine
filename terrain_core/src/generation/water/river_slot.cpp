// The slot of a fall in steps and the blocks of its pools (river_slot.hpp).
#include "river_slot.hpp"

#include "river_lips.hpp"
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
// channel by kSlotWander of the width of its walls and of its channel over kWanderWave meters.
// Each noise (slotNoise) is a gradient noise, kGradientShare of it, plus a value noise on the
// same lattice, kValueShare of it, shifted along the course by a draw: no zero at regular
// intervals along the slot (a row of teeth), about a half at most as the gradient noise alone.
constexpr double kSlotWave = 40.0;
constexpr double kSlotSwing = 0.45;
constexpr double kEdgeWave = 7.0;
constexpr double kEdgeSwing = 0.15;
constexpr double kNarrowWave = 30.0;
constexpr double kNarrowMost = 0.2;
constexpr double kWanderWave = 55.0;
constexpr double kSlotWander = 0.25;
constexpr double kGradientShare = 0.6;
constexpr double kValueShare = 0.2;
// The walls never reach less than kRunLeast of their width beyond the channel; a ledge along the
// water rising kLedgeRise meters per meter, then a cliff over the last kCliffShare of them.
constexpr double kRunLeast = 0.25;
constexpr double kLedgeRise = 0.05;
constexpr double kCliffShare = 0.3;
// The spurs of rock: rare, of all sizes and spacings. In kSpurChance of the cells of kSpurCell
// meters along each side, one anywhere in it, kSpurLeast to kSpurMost meters long on each side
// of its middle (mostly short), jutting out by kSpurShare to kSpurShare + kSpurSpread of the
// walls there.
constexpr double kSpurCell = 50.0;
constexpr double kSpurChance = 0.5;
constexpr double kSpurLeast = 3.0;
constexpr double kSpurMost = 14.0;
constexpr double kSpurShare = 0.15;
constexpr double kSpurSpread = 0.4;
// The blocks: in heaps at the foot of the drops of kHeapLeast meters at least, kBlockEnds meters
// at least from the ends of the course (another reach carries on there), in channels
// kBlockHalfLeast meters wide at least on each side of their axis. A heap holds a block per
// kHeapDrop meters of the drop (plus a draw), kHeapMost at most, around a point kHeapNear to
// kHeapFar times the drop below the foot (within kPoolShare of the pool there), kHeapSpread plus
// kHeapGrowth of the drop around it. Each block kBlockLeast to kBlockLeast + kBlockSpread meters
// wide (mostly small), never more than kBlockHalf of the channel nor kBlockDrop of the drop plus
// kBlockLeast; kBlockOutShare of them stand kBlockOutLeast to kBlockOutLeast + kBlockOutSpread
// meters out of their water, the others sunk by up to kBlockSunk; never within kBlockLip of the
// level of the step above them.
constexpr double kHeapLeast = 3.0;
constexpr double kBlockEnds = 30.0;
constexpr double kBlockHalfLeast = 2.5;
constexpr double kHeapDrop = 3.0;
constexpr int kHeapMost = 6;
constexpr double kHeapNear = 0.4;
constexpr double kHeapFar = 1.2;
constexpr double kPoolShare = 0.7;
constexpr double kHeapSpread = 2.0;
constexpr double kHeapGrowth = 0.25;
constexpr double kBlockLeast = 1.5;
constexpr double kBlockSpread = 5.5;
constexpr double kBlockHalf = 0.7;
constexpr double kBlockDrop = 0.6;
constexpr double kBlockOutShare = 0.45;
constexpr double kBlockOutLeast = 1.0;
constexpr double kBlockOutSpread = 3.0;
constexpr double kBlockSunk = 1.0;
constexpr double kBlockLip = 0.5;
constexpr double kBlockClear = 0.5; // their edge below the foot of the face above them
constexpr double kPoolTail = 0.3;
constexpr double kBlockBelow = 2.0; // their foot below the bed
constexpr double kBlockAbove = 0.5; // their top above it at least
// A block stands kBlockTall times its width above the bed at most, kBlockOutWidth times it out of
// the water: the finest meshes (cells of 1 m) reach its top.
constexpr double kBlockTall = 1.2;
constexpr double kBlockOutWidth = 0.6;

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
        const double u = shareOf(bits, 40);
        const double length = kSpurLeast + (kSpurMost - kSpurLeast) * u * u;
        const double d = std::abs(s - middle) / length;
        if (d < 1.0) {
            const double share =
                kSpurShare + kSpurSpread * shareOf(noise::deriveSeed(bits, 1), 0);
            spur = std::max(spur, share * (1.0 - d * d));
        }
    }
    return spur;
}

// Whether the water of c comes down a face from point i to the next.
bool faceAt(const std::vector<CoursePoint>& p, std::size_t i) {
    return p[i].fall && p[i + 1].fall &&
           p[i].level - p[i + 1].level > kLipSlope * (p[i + 1].s - p[i].s);
}

// The heap of blocks at the foot of the drop of c from point lip to point foot, the next lip at
// next meters along it: the k-th block of the course next.
void placeHeap(Course& c, std::size_t lip, std::size_t foot, double next, std::uint64_t& k) {
    const std::vector<CoursePoint>& p = c.points;
    const double drop = static_cast<double>(p[lip].level - p[foot].level);
    const double level = p[foot].level;
    const double cap = static_cast<double>(p[lip].level) - kBlockLip;
    const std::uint64_t seed = noise::deriveSeed(c.slotSeed, kBlocks);
    const std::uint64_t heap = noise::deriveSeed(seed, ++k);
    const int count =
        std::min(kHeapMost, static_cast<int>(std::floor(drop / kHeapDrop + shareOf(heap, 0))));
    const double room = kPoolShare * (next - p[foot].s);
    const double middle =
        p[foot].s + std::min(room, drop * (kHeapNear + (kHeapFar - kHeapNear) * shareOf(heap, 24)));
    const double spread = kHeapSpread + kHeapGrowth * drop;
    const double across = 2.0 * shareOf(heap, 40) - 1.0;
    const std::size_t first = c.blocks.size();
    for (int n = 0; n < count; ++n) {
        const std::uint64_t bits = noise::deriveSeed(seed, ++k);
        const double u = shareOf(bits, 24);
        double width =
            std::min(kBlockLeast + kBlockSpread * u * u, kBlockDrop * drop + kBlockLeast);
        // Below the face: the whole block in the pool, never over the falling water, nor in its
        // last kPoolTail where the next lip may recede (river_lips.hpp).
        const double low = p[foot].s + 0.5 * width + kBlockClear;
        const double high = std::min(p[foot].s + room, next - kPoolTail * (next - p[foot].s) -
                                                           0.5 * width - kBlockClear);
        if (!(high >= low)) {
            continue;
        }
        const double s = std::clamp(middle + spread * (2.0 * shareOf(bits, 0) - 1.0), low, high);
        if (s < p.front().s + kBlockEnds || s > p.back().s - kBlockEnds) {
            continue;
        }
        std::size_t q = foot;
        while (q + 2 < p.size() && p[q + 1].s < s) {
            ++q;
        }
        const CoursePoint& a = p[q];
        const CoursePoint& b = p[q + 1];
        const double t = b.s > a.s ? std::clamp((s - a.s) / (b.s - a.s), 0.0, 1.0) : 0.0;
        const double dx = b.x - a.x;
        const double dz = b.z - a.z;
        const double along = dmath::hypot(dx, dz);
        const double half = slotHalf(c.slotSeed, s, lerp(a.half, b.half, t));
        if (!(along > 0.0) || half < kBlockHalfLeast) {
            continue;
        }
        width = std::min(width, kBlockHalf * half);
        const double radius = 0.5 * width;
        const double free = 0.8 * (half - radius);
        const double aside =
            std::clamp(0.5 * across * half + spread * (2.0 * shareOf(bits, 40) - 1.0), -free, free);
        // The bed of the channel there (river_profile.cpp): the block stands above it.
        const double off = aside / half;
        const double bed =
            level - static_cast<double>(lerp(a.depth, b.depth, t)) * (1.0 - off * off);
        const std::uint64_t more = noise::deriveSeed(bits, 1);
        const double top =
            shareOf(more, 0) < kBlockOutShare
                ? level + kBlockOutLeast + kBlockOutSpread * shareOf(more, 24)
                : level - kBlockSunk * shareOf(more, 24);
        const double base = bed - kBlockBelow;
        const double highest =
            std::min({cap, bed + kBlockTall * width, level + kBlockOutWidth * width});
        const double placed = std::min(std::max(top, bed + kBlockAbove), highest);
        const double x = lerp(a.x, b.x, t) - aside * dz / along;
        const double z = lerp(a.z, b.z, t) + aside * dx / along;
        // Never on another block of the heap: each stands on the bed, its top its own.
        const bool apart = std::all_of(c.blocks.begin() + static_cast<std::ptrdiff_t>(first),
                                       c.blocks.end(), [&](const FallBlock& o) {
                                           return dmath::hypot(o.x - x, o.z - z) >=
                                                  radius + static_cast<double>(o.radius);
                                       });
        if (!(placed > base + 0.5) || !apart) {
            continue;
        }
        c.blocks.push_back({x, z, static_cast<float>(radius), static_cast<float>(placed),
                            static_cast<float>(base)});
    }
}

} // namespace

float slotBase(double mean) {
    return static_cast<float>(std::clamp(kSlotShare * mean, kSlotLeast, kSlotMost));
}

double slotNoise(std::uint64_t seed, std::uint64_t stream, double s, double wave) {
    const std::uint64_t key = noise::deriveSeed(seed, stream);
    const double phase = s / wave + shareOf(key, 40);
    const double i = std::floor(phase);
    const double f = phase - i;
    const auto k = static_cast<std::int64_t>(i);
    const std::uint64_t h0 = noise::deriveSeed(key, static_cast<std::uint64_t>(k));
    const std::uint64_t h1 = noise::deriveSeed(key, static_cast<std::uint64_t>(k + 1));
    const double w = f * f * f * (f * (6.0 * f - 15.0) + 10.0);
    // A gradient noise (0 at the lattice) plus a value noise (a draw there), from the same draws.
    const double g0 = 2.0 * shareOf(h0, 0) - 1.0;
    const double g1 = 2.0 * shareOf(h1, 0) - 1.0;
    const double gradient = g0 * f + w * (g1 * (f - 1.0) - g0 * f);
    const double v0 = 2.0 * shareOf(h0, 24) - 1.0;
    const double value = v0 + w * (2.0 * shareOf(h1, 24) - 1.0 - v0);
    return std::clamp(kGradientShare * gradient + kValueShare * value, -1.0, 1.0);
}

double slotHalf(std::uint64_t seed, double s, double half) {
    return half * (1.0 - kNarrowMost * 0.5 * (1.0 + slotNoise(seed, kNarrow, s, kNarrowWave)));
}

double slotRun(std::uint64_t seed, double s, double base, double half, double side) {
    const bool left = side >= 0.0;
    const double width = base * (1.0 + kSlotSwing * slotNoise(seed, left ? kWidthLeft : kWidthRight,
                                                            s, kSlotWave) +
                                 kEdgeSwing * slotNoise(seed, left ? kEdgeLeft : kEdgeRight, s,
                                                      kEdgeWave));
    const double spur = spurAt(noise::deriveSeed(seed, left ? kSpurLeft : kSpurRight), s);
    // The ravine wanders about its channel: one wall nearer as the other recedes.
    const double wander = kSlotWander * (base + half) * slotNoise(seed, kWander, s, kWanderWave);
    return std::max(width * (1.0 - spur) + (left ? wander : -wander), kRunLeast * base);
}

double slotReach(double base, double half) {
    // The noises within [-1, 1] (slotNoise), the spurs only narrowing it.
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
    // The drops: from the first point of a run of faces (the lip) to its last (the foot).
    std::vector<std::size_t> lips;
    std::vector<std::size_t> feet;
    for (std::size_t i = 0; i + 1 < p.size(); ++i) {
        if (!faceAt(p, i)) {
            continue;
        }
        std::size_t j = i + 1;
        while (j + 1 < p.size() && faceAt(p, j)) {
            ++j;
        }
        if (static_cast<double>(p[i].level - p[j].level) >= kHeapLeast) {
            lips.push_back(i);
            feet.push_back(j);
        }
        i = j - 1;
    }
    std::uint64_t k = 0;
    for (std::size_t d = 0; d < lips.size(); ++d) {
        const double next = d + 1 < lips.size() ? p[lips[d + 1]].s : p.back().s;
        placeHeap(c, lips[d], feet[d], next, k);
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
            // Flatter on top and steeper at its sides than a dome: a block.
            const double rise = static_cast<double>(b.top) - b.base;
            const double share = d2 / r2;
            top = std::max(top, static_cast<double>(b.top) - rise * share * std::sqrt(share));
        }
    }
    return top;
}

} // namespace terrain::water

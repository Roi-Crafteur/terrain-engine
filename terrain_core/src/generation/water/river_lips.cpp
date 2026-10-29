// The lips of the falls in steps (river_lips.hpp).
#include "river_lips.hpp"

#include "river_slot.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace terrain::water {

namespace {

// The hardness of the lip is read kCapBelow meters under its water, that under it (the least) at
// kUnder shares of the drop below the lip, the last one under its foot (the rock its pool wears).
constexpr double kCapBelow = 0.5;
constexpr std::array<double, 3> kUnder = {0.5, 1.0, 1.5};
// The middle of the edge recedes the height of the drop times kRecessBase plus kRecessContrast of
// the contrast of their hardness, times the share of the discharge (kFlowLow at kFlowRef m^3/s and
// below, kFlowGrowth more per tenfold of it, kFlowHigh at most); within kRecessWidth of its width,
// and within the pools: kPoolAbove of the pool above (the valley moves from twice its recess
// upstream of the edge, which reaches 1.3 times the recess with its roughness) and kPoolBelow of
// the pool below its face (the valley comes back over twice the recess below the lip, plus the
// face).
constexpr double kRecessBase = 0.1;
constexpr double kRecessContrast = 2.5;
constexpr double kFlowRef = 10.0;
constexpr double kFlowLow = 0.6;
constexpr double kFlowGrowth = 0.3;
constexpr double kFlowHigh = 1.4;
constexpr double kRecessWidth = 1.2;
constexpr double kPoolAbove = 0.25;
constexpr double kPoolBelow = 1.0 / 2.5;
// The width of the edge: its channel and kWallShare of the walls of its slot on each side. Its
// irregularity: kEdgeBase plus kEdgeShare of the drop, kEdgeMost meters at most, over waves of
// kEdgeWave meters across; the recess of the middle varies by up to a half of kRoughShare of it
// over kRoughWave (slotNoise, within about a half).
constexpr double kWallShare = 0.7;
constexpr double kEdgeBase = 0.3;
constexpr double kEdgeShare = 0.06;
constexpr double kEdgeMost = 1.5;
constexpr double kEdgeWave = 3.0;
constexpr double kRoughShare = 0.6;
constexpr double kRecessMost = 1.0 + 0.5 * kRoughShare; // lipRecess within it
constexpr double kRoughWave = 7.0;
// Without a pool around it (the first or the last lip of a course), kOpenPool meters.
constexpr double kOpenPool = 40.0;
constexpr std::uint64_t kLipSalt = 0x6c6970735f313241ULL;

double smooth(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

bool isFace(const CoursePoint& a, const CoursePoint& b) {
    return a.fall && b.fall && a.level - b.level > kLipSlope * (b.s - a.s);
}

// The least hardness of the rock of relief under the lip of a drop at (x, z), from top to bottom
// water, and the hardness of the lip.
double contrastAt(const Heightfield& relief, double x, double z, double top, double bottom) {
    const double cap = relief.rockHardness(x, z, top - kCapBelow);
    double under = 1.0;
    for (const double share : kUnder) {
        under = std::min(under, static_cast<double>(
                                    relief.rockHardness(x, z, top - share * (top - bottom))));
    }
    return std::clamp(cap - under, 0.0, 1.0);
}

// The edge of lip l at its pools above and below (meters along the course), of a river of
// discharge.
void shapeLip(FallLip& l, const Course& c, double above, double below, double discharge) {
    const CoursePoint& p = c.points[l.point];
    const double drop = static_cast<double>(l.top) - l.bottom;
    const double flow = std::clamp(
        kFlowLow + kFlowGrowth * dmath::log(std::max(discharge, kFlowRef) / kFlowRef) /
                       dmath::log(10.0),
        kFlowLow, kFlowHigh);
    l.width = static_cast<float>(slotHalf(c.slotSeed, l.s, p.half) + kWallShare * p.slot);
    const double room = std::min({kRecessWidth * l.width, kPoolAbove * above, kPoolBelow * below});
    l.recess = static_cast<float>(std::clamp(
        drop * (kRecessBase + kRecessContrast * static_cast<double>(l.contrast)) * flow, 0.0,
        std::max(0.0, room)));
    l.edge = static_cast<float>(
        std::clamp(std::min(kEdgeBase + kEdgeShare * drop, kEdgeMost), 0.0, std::max(0.0, room)));
    l.recess = std::min(l.recess, static_cast<float>(std::max(0.0, room)) - l.edge);
    l.recess = std::max(l.recess, 0.0f);
}

} // namespace

void placeLips(Course& c, const Heightfield& relief, double discharge) {
    c.lips.clear();
    const std::vector<CoursePoint>& p = c.points;
    // The drops: from the first point of a run of faces (the lip) to its last (the foot).
    std::vector<std::array<std::size_t, 2>> drops;
    for (std::size_t i = 0; i + 1 < p.size(); ++i) {
        if (!isFace(p[i], p[i + 1])) {
            continue;
        }
        std::size_t j = i + 1;
        while (j + 1 < p.size() && isFace(p[j], p[j + 1])) {
            ++j;
        }
        if (static_cast<double>(p[i].level - p[j].level) >= kLipLeast) {
            drops.push_back({i, j});
        }
        i = j - 1;
    }
    for (std::size_t k = 0; k < drops.size(); ++k) {
        const CoursePoint& a = p[drops[k][0]];
        const CoursePoint& b = p[drops[k][0] + 1];
        const CoursePoint& foot = p[drops[k][1]];
        FallLip l;
        l.x = a.x;
        l.z = a.z;
        const double length = dmath::hypot(b.x - a.x, b.z - a.z);
        l.dirX = length > 0.0 ? (b.x - a.x) / length : 1.0;
        l.dirZ = length > 0.0 ? (b.z - a.z) / length : 0.0;
        l.s = a.s;
        l.point = static_cast<std::uint32_t>(drops[k][0]);
        l.top = a.level;
        l.bottom = foot.level;
        l.contrast = static_cast<float>(contrastAt(relief, a.x, a.z, a.level, foot.level));
        l.seed = noise::deriveSeed(noise::deriveSeed(kLipSalt, c.slotSeed), k);
        const double above = k > 0 ? a.s - p[drops[k - 1][1]].s : kOpenPool;
        const double below =
            (k + 1 < drops.size() ? p[drops[k + 1][0]].s : foot.s + kOpenPool) - foot.s;
        shapeLip(l, c, above, below, discharge);
        l.reach = static_cast<float>(2.0 * (l.recess + l.edge) + (foot.s - a.s));
        if (l.recess + l.edge > 0.0f) {
            c.lips.push_back(l);
        }
    }
    c.lipsMinX = c.lipsMinZ = std::numeric_limits<double>::infinity();
    c.lipsMaxX = c.lipsMaxZ = -std::numeric_limits<double>::infinity();
    for (const FallLip& l : c.lips) {
        const double r = 4.0 * static_cast<double>(l.recess + l.edge) + l.reach + l.width;
        c.lipsMinX = std::min(c.lipsMinX, l.x - r);
        c.lipsMinZ = std::min(c.lipsMinZ, l.z - r);
        c.lipsMaxX = std::max(c.lipsMaxX, l.x + r);
        c.lipsMaxZ = std::max(c.lipsMaxZ, l.z + r);
    }
}

double lipRecess(const FallLip& l, double side) {
    const double u = side / static_cast<double>(l.width);
    if (!(std::abs(u) < 1.0)) {
        return 0.0;
    }
    const double across = 1.0 - u * u;
    const double rough = slotNoise(l.seed, 1, side, kRoughWave);
    const double edge = slotNoise(l.seed, 2, side, kEdgeWave);
    return static_cast<double>(l.recess) * across * across * (1.0 + kRoughShare * rough) +
           static_cast<double>(l.edge) * across * (0.5 + edge);
}

void cutLips(const Course& c, double x, double z, const LipValley& near, Valley& v) {
    if (c.lips.empty() || !(v.height < std::numeric_limits<double>::infinity()) ||
        x < c.lipsMinX || x > c.lipsMaxX || z < c.lipsMinZ ||
        z > c.lipsMaxZ) {
        return;
    }
    for (const FallLip& l : c.lips) {
        const double dx = x - l.x;
        const double dz = z - l.z;
        const double along = dx * l.dirX + dz * l.dirZ;
        const double side = l.dirX * dz - l.dirZ * dx;
        // The valley moved downstream by the recess from twice it upstream of the edge (in the
        // pool above: the water coming down to the first lip of a stretch at most 1.75 times as
        // steep there) to the lip, back over reach below it: the course in order still, from the
        // pool above to the pool below. The recess is kRecessMost of that of the lip at most.
        const double reach = l.reach;
        const double most = kRecessMost * static_cast<double>(l.recess + l.edge);
        if (!(std::abs(side) < l.width) || along < -3.0 * most || along > reach) {
            continue;
        }
        const double recess = lipRecess(l, side);
        if (!(recess > 0.0) || along < -3.0 * recess) {
            continue;
        }
        const double shift = recess * smooth(-3.0 * recess, -recess, along) *
                             (1.0 - smooth(0.0, reach, along));
        if (!(shift > 0.0)) {
            continue;
        }
        // In the channel, only where the water stands there too (the pool below may turn away
        // from the line of the lip: its banks never cut across the falling water).
        const Valley moved = near(x + shift * l.dirX, z + shift * l.dirZ, l);
        if (moved.height < v.height && (moved.wet || !v.wet)) {
            v.height = moved.height;
            v.level = moved.level;
            v.floorLevel = moved.floorLevel;
            v.wet = moved.wet;
        }
    }
}

} // namespace terrain::water

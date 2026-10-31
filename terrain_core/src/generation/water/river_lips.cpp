// The lips of the falls in steps (river_lips.hpp).
#include "river_lips.hpp"

#include "river_slot.hpp"
#include "river_winding.hpp"
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
// and within the pools: kPoolAbove of the pool above (the valley moves from 1.5 times its most
// recess upstream of the edge: recessMost) and kPoolBelow of the pool below its face (the valley
// comes back over 1.5 times its most recess below the lip, plus the face).
constexpr double kRecessBase = 0.1;
constexpr double kRecessContrast = 2.5;
constexpr double kFlowRef = 10.0;
constexpr double kFlowLow = 0.6;
constexpr double kFlowGrowth = 0.3;
constexpr double kFlowHigh = 1.4;
constexpr double kRecessWidth = 1.2;
constexpr double kPoolAbove = 0.5;
constexpr double kPoolBelow = 0.5;
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
// Its edge turns from across the slot by up to kTurnMost (35 degrees, drawn), as far as the pools
// leave room for (in the frame of the slot: river_winding.hpp); the line of its turn bends back
// to the line of its middle over the outer kTurnFade of its width (the walls of its slot).
constexpr double kTurnMost = 35.0 / 360.0;
constexpr double kTurnFade = 0.4;
// Its bow: kBowLeast to kBowMost of its width (drawn), its middle upstream over hard rock on soft
// (kBowContrast at least: an amphitheater), either way over rock of one hardness; and two waves
// across it, of kWaveLeast to kWaveMost of its width each (drawn), kWaveShare of it high at most
// each. All within the room its pools leave, the waves first, then the bow, then the turn.
constexpr double kBowLeast = 0.15;
constexpr double kBowMost = 0.4;
constexpr double kBowContrast = 0.3;
constexpr double kWaveLeast = 0.3;
constexpr double kWaveMost = 0.7;
constexpr double kWaveShare = 0.07;
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
    // Its channel narrowed at its sill (river_slot.hpp: bowlAt).
    l.channel = static_cast<float>(slotHalf(c.slotSeed, l.s, p.half) * (1.0 + bowlAt(c, l.s).wider));
    l.width = static_cast<float>(static_cast<double>(l.channel) + kWallShare * p.slot);
    const double room = std::min({kRecessWidth * l.width, kPoolAbove * above, kPoolBelow * below});
    l.recess = static_cast<float>(std::clamp(
        drop * (kRecessBase + kRecessContrast * static_cast<double>(l.contrast)) * flow, 0.0,
        std::max(0.0, room)));
    l.edge = static_cast<float>(
        std::clamp(std::min(kEdgeBase + kEdgeShare * drop, kEdgeMost), 0.0, std::max(0.0, room)));
    l.recess = std::min(l.recess, static_cast<float>(std::max(0.0, room)) - l.edge);
    l.recess = std::max(l.recess, 0.0f);
    // Its waves, then its bow, in the room left (shares of its whole width).
    const auto share = [&l](std::uint64_t stream) {
        return static_cast<double>(noise::deriveSeed(l.seed, stream) >> 40) * 0x1.0p-24;
    };
    const double width = static_cast<double>(l.width);
    const double across = 2.0 * static_cast<double>(l.channel);
    double left = std::max(0.0, room - static_cast<double>(l.recess + l.edge));
    l.waves = static_cast<float>(std::min(kWaveShare * across, 0.25 * left));
    l.waveA = static_cast<float>(across * (kWaveLeast + (kWaveMost - kWaveLeast) * share(4)));
    l.waveB = static_cast<float>(across * (kWaveLeast + (kWaveMost - kWaveLeast) * share(5)));
    left -= 2.0 * static_cast<double>(l.waves);
    const double bow = std::min(across * (kBowLeast + (kBowMost - kBowLeast) * share(6)), left);
    const bool hollow = static_cast<double>(l.contrast) >= kBowContrast || share(7) < 0.5;
    l.bow = static_cast<float>(hollow ? bow : -bow);
    left -= bow;
    // The turn of its edge: an angle to the slot drawn, the slot itself at an angle to the axis
    // where it winds (rate: its tangent), within the room the pools leave on its farther side.
    const double angle =
        kTurnMost * (2.0 * static_cast<double>(noise::deriveSeed(l.seed, 3) >> 40) * 0x1.0p-24 -
                     1.0);
    const double rate = shiftAt(c, l.s).rate;
    const double turn = (rate + dmath::sinTurns(angle) / dmath::cosTurns(angle)) /
                        std::sqrt(1.0 + rate * rate);
    const double free = std::max(0.0, left) / (2.0 * width);
    l.turn = static_cast<float>(std::clamp(turn, -free, free));
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
        l.reach = static_cast<float>(1.5 * recessMost(l) + (foot.s - a.s));
        if (recessMost(l) > 0.0) {
            c.lips.push_back(l);
        }
    }
    c.lipsMinX = c.lipsMinZ = std::numeric_limits<double>::infinity();
    c.lipsMaxX = c.lipsMaxZ = -std::numeric_limits<double>::infinity();
    for (FallLip& l : c.lips) {
        // Its slot as far aside and stretched as it winds (river_winding.hpp), all along what it
        // cuts (cutLips).
        const double from = l.s - 1.5 * recessMost(l) - kWindWindow;
        const double to = l.s + static_cast<double>(l.reach) + kWindWindow;
        float aside = 0.0f;
        for (const CoursePoint& q : c.points) {
            aside = q.s > from && q.s < to ? std::max(aside, q.windMost) : aside;
        }
        const double r = 4.0 * recessMost(l) + l.reach + kStretchMost * l.width + aside;
        l.around = static_cast<float>(r + 1.0);
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
    const double turn = static_cast<double>(l.turn);
    const double fade = 1.0 - smooth(1.0 - kTurnFade, 1.0, std::abs(u));
    const double turned = (turn > 0.0 ? turn * (side + l.width) : -turn * (l.width - side)) * fade;
    // Its bow and its waves over its channel, back to the line of its middle within the walls of
    // its slot (inside: 1 in its channel, 0 at its width).
    const double v = std::min(1.0, std::abs(side) / std::max(1e-6, static_cast<double>(l.channel)));
    const double inside =
        1.0 - smooth(static_cast<double>(l.channel), static_cast<double>(l.width), std::abs(side));
    const double bow = static_cast<double>(l.bow);
    // As bent all across its channel (no straight piece), meeting its walls at an angle.
    const double bowed = bow > 0.0 ? bow * (1.0 - v * v) : -bow * v * v * inside;
    const double waves = static_cast<double>(l.waves) * inside *
                         (2.0 + slotNoise(l.seed, 4, side, l.waveA) +
                          slotNoise(l.seed, 5, side, l.waveB)) * 0.5;
    return static_cast<double>(l.recess) * across * across * (1.0 + kRoughShare * rough) +
           static_cast<double>(l.edge) * across * (0.5 + edge) + turned + bowed + waves;
}

double recessMost(const FallLip& l) {
    // The noises within [-1, 1] (slotNoise).
    return kRecessMost * static_cast<double>(l.recess) + 1.5 * static_cast<double>(l.edge) +
           std::abs(static_cast<double>(l.bow)) + 2.0 * static_cast<double>(l.waves) +
           2.0 * std::abs(static_cast<double>(l.turn)) * l.width;
}

std::vector<std::uint32_t> lipsNear(const Course& c, double cx, double cz, double reach) {
    std::vector<std::uint32_t> out;
    for (std::uint32_t n = 0; n < c.lips.size(); ++n) {
        const FallLip& l = c.lips[n];
        if (dmath::hypot(cx - l.x, cz - l.z) < reach + static_cast<double>(l.around)) {
            out.push_back(n);
        }
    }
    return out;
}

void cutLips(const Course& c, double x, double z, const LipValley& near, Valley& v,
             const std::vector<std::uint32_t>* lips) {
    if (c.lips.empty() || !(v.height < std::numeric_limits<double>::infinity()) ||
        x < c.lipsMinX || x > c.lipsMaxX || z < c.lipsMinZ ||
        z > c.lipsMaxZ) {
        return;
    }
    const std::size_t count = lips != nullptr ? lips->size() : c.lips.size();
    for (std::size_t n = 0; n < count; ++n) {
        const FallLip& l = c.lips[lips != nullptr ? (*lips)[n] : n];
        const double dx = x - l.x;
        const double dz = z - l.z;
        const double along = dx * l.dirX + dz * l.dirZ;
        // The valley moved downstream by the recess from 1.5 times it upstream of the edge (in
        // the pool above) to the lip, back over reach below it: the course in order still, from
        // the pool above to the pool below. The recess is recessMost at most.
        const double reach = l.reach;
        const double most = recessMost(l);
        if (along < -1.5 * most || along > reach) {
            continue;
        }
        // Across the slot where it winds (river_winding.hpp).
        const double side = slotFrame(c, l.s + along, l.dirX * dz - l.dirZ * dx).side;
        if (!(std::abs(side) < l.width)) {
            continue;
        }
        const double recess = lipRecess(l, side);
        if (!(recess > 0.0) || along < -1.5 * recess) {
            continue;
        }
        const double shift = recess * smooth(-1.5 * recess, -recess, along) *
                             (1.0 - smooth(0.0, reach, along));
        if (!(shift > 0.0)) {
            continue;
        }
        // Along the slot, where it winds: the point keeps its side of it. In the channel, only
        // where the water stands there too (the pool below may turn away from the line of the
        // lip: its banks never cut across the falling water).
        const double aside = shiftAt(c, l.s + along).rate;
        const Valley moved = near(x + shift * (l.dirX - aside * l.dirZ),
                                  z + shift * (l.dirZ + aside * l.dirX), l);
        if (moved.height < v.height && (moved.wet || !v.wet)) {
            v.height = moved.height;
            v.level = moved.level;
            v.floorLevel = moved.floorLevel;
            v.wet = moved.wet;
        }
    }
}

} // namespace terrain::water

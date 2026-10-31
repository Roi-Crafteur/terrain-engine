// The lips of the steps of the falls of the small world (phase 12A): never a straight cut across
// their slot. Each lip is found at kPlaces places across its channel (within kAcross of its half
// width on each side), where the ground comes down through the middle of its drop.
//   - Over hard rock on soft (the contrast of Heightfield::rockHardness kContrast at least), the
//     middle of the lip lies upstream of its sides by kRecessLeast of the drop at least on average
//     (an amphitheater).
//   - Bowed: the farthest of its places from the line through its sides kBowedLeast of its width
//     off it at least, at kBowedShare of the lips whose pools leave room (half of each kRoomLeast
//     of their width at least, the pool above down to the rapids above it: river_lips.cpp); a
//     straight piece (kPiece places on a line, within kOnLine of the width or kOnLineLeast meters)
//     at kStraightShare of them at most, and at kStraightAll of all the lips at most.
//   - Turned: the line through its places kTurnAcross of the half width off its axis turns from
//     across the slot by more than kTurnLeast degrees at kTurnedShare of the lips at least,
//     kTurnMost at most.
#include "falls_support.hpp"
#include "steps_support.hpp"

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
using steps::alongOf;
using steps::Drop;
using steps::dropsOf;
using terrain::RiverPoint;
using terrain::RiverReach;

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
constexpr double kStraightAll = 0.1;
constexpr double kOpenRoom = 40.0;
constexpr double kTurnAcross = 0.6;
constexpr double kTurnLeast = 10.0;
constexpr double kTurnedShare = 0.6;
constexpr double kTurnMost = 45.0;

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
        under = std::min(
            under, static_cast<double>(w.relief.rockHardness(x, z, top - share * (top - bottom))));
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

// Meters of pool above point i of p, from the foot of the drop above at from (meters along the
// reach) at most, up to the rapids above it if any (their water coming down more than kPoolSlope
// meters per meter).
constexpr double kPoolSlope = 0.05;

double poolAbove(const std::vector<RiverPoint>& p, const std::vector<double>& s, std::size_t i,
                 double from) {
    for (std::size_t j = i; j > 0 && s[j - 1] >= from; --j) {
        if (static_cast<double>(p[j - 1].level - p[j].level) > kPoolSlope * (s[j] - s[j - 1])) {
            return s[i] - s[j];
        }
    }
    return s[i] - from;
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
            // The room of its pools: half of the pool above (down to the rapids above it, if
            // any: river_lips.cpp), half of the pool below.
            const double above =
                poolAbove(p, s, i, k > 0 ? s[drops[k - 1].foot] : s[i] - kOpenRoom);
            const double below =
                k + 1 < drops.size() ? s[drops[k + 1].lip] - s[drops[k].foot] : kOpenRoom;
            const bool roomy =
                0.5 * std::min(above, below) >= kRoomLeast * static_cast<double>(p[i].width);
            measureLip(w, reach, i, roomy, m);
        }
    }
    const auto shareBowed = [](const std::vector<double>& bows) {
        std::size_t bowed = 0;
        for (const double b : bows) {
            bowed += b >= kBowedLeast ? 1 : 0;
        }
        return static_cast<double>(bowed) /
               static_cast<double>(std::max<std::size_t>(1, bows.size()));
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
    TEST_CHECK(m.hollow.size() >= 3 && m.bowed.size() >= 100 && m.turns.size() >= 100);
    TEST_CHECK(meanOf(m.hollow) >= kRecessLeast);
    TEST_CHECK(m.roomyBowed.size() >= 50 && roomyShare >= kBowedShare);
    TEST_CHECK(static_cast<double>(m.roomyStraight) <=
               kStraightShare * static_cast<double>(m.roomyBowed.size()));
    TEST_CHECK(static_cast<double>(m.straight) <=
               kStraightAll * static_cast<double>(m.bowed.size()));
    TEST_CHECK(share >= kTurnedShare && most <= kTurnMost);
}

} // namespace

int main() {
    const World w = falls::makeWorld();
    Reaches reaches(*w.hydrology);
    testLips(w, reaches, falls::steepReaches(w, reaches));
    return test::result();
}

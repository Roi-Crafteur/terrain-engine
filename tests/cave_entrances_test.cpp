// Surface entrances of the caves: deterministic placement, geometry of the tube and the
// chamber, an air path from the sky down to the chamber, and no entrance in the sea, on the
// shore or near a river. Entrances in the density of the caves: caves_test.cpp; under a region
// with rivers: cave_region_test.cpp. Every parameter is read from the field.
#include "terrain/generation/cave_entrances.hpp"
#include "terrain/generation/cave_sampler.hpp"

#include "caves_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <utility>
#include <vector>

namespace {

using caves_test::carvedAir;
using caves_test::flatGround;
using caves_test::heightfieldGround;
using caves_test::kSeed;
using caves_test::Lattice;
using caves_test::Point;
using caves_test::pointNearEntrance;
using caves_test::Rng;
using caves_test::SkyFill;
using caves_test::terrainDensity;
using caves_test::withStraightRiver;
using terrain::CaveColumn;
using terrain::CaveDetail;
using terrain::CaveEntrance;
using terrain::CaveField;
using terrain::CaveGround;
using terrain::CaveParams;
using terrain::Heightfield;

constexpr double kHalf = 4096.0;

std::vector<CaveEntrance> entrancesOver(const CaveField& field, const CaveGround& ground,
                                        double half) {
    return field.entrancesNear(-half, -half, half, half, ground);
}

double largestRadius(const CaveEntrance& e) {
    return static_cast<double>(std::max(e.tubeRadius, e.chamberRadius));
}

// Point of the axis of the entrance at the fraction t from its top to its bottom, seen from
// above.
std::pair<double, double> axisAt(const CaveEntrance& e, double t) {
    return {e.topX + t * (e.bottomX - e.topX), e.topZ + t * (e.bottomZ - e.topZ)};
}

// ---------------------------------------------------------------------------

// Entrances exist, in cell order, each in its cell and as the field places it alone; a
// rectangle gives the entrances of the list that reach it; the same field gives the same list
// and another seed another list.
void testEntrancesExist() {
    const Heightfield heightfield(kSeed);
    const CaveGround ground = heightfieldGround(heightfield);
    const CaveField field(kSeed);
    const std::vector<CaveEntrance> list = entrancesOver(field, ground, kHalf);
    const double spacing = field.params().entrances.spacing;
    const double jitter = field.params().entrances.jitter;
    bool ordered = true;
    bool inCell = true;
    bool alone = true;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const CaveEntrance& e = list[i];
        ordered = ordered && (i == 0 || std::make_pair(list[i - 1].cellZ, list[i - 1].cellX) <
                                            std::make_pair(e.cellZ, e.cellX));
        inCell = inCell && std::abs(e.topX / spacing - e.cellX - 0.5) <= jitter / 2.0 + 1.0e-9 &&
                 std::abs(e.topZ / spacing - e.cellZ - 0.5) <= jitter / 2.0 + 1.0e-9;
        CaveEntrance placed;
        alone = alone && field.entrance(e.cellX, e.cellZ, ground, placed) && placed == e;
    }
    // The part west of the median site: it holds every entrance whose site lies in it, and
    // only entrances of the list.
    std::vector<double> sites;
    for (const CaveEntrance& e : list) {
        sites.push_back(e.topX);
    }
    std::sort(sites.begin(), sites.end());
    const double split = sites.empty() ? 0.0 : sites[sites.size() / 2];
    const std::vector<CaveEntrance> west =
        field.entrancesNear(-kHalf, -kHalf, split, kHalf, ground);
    const auto listed = [](const std::vector<CaveEntrance>& entrances, const CaveEntrance& e) {
        return std::find(entrances.begin(), entrances.end(), e) != entrances.end();
    };
    bool subset = true;
    for (const CaveEntrance& e : west) {
        subset = subset && listed(list, e);
    }
    for (const CaveEntrance& e : list) {
        subset = subset && (e.topX > split || listed(west, e));
    }
    std::printf("entrances: %zu over [-%.0f, %.0f]^2, %zu west of x = %.0f\n", list.size(), kHalf,
                kHalf, west.size(), split);
    TEST_CHECK(list.size() >= 20);
    TEST_CHECK(ordered && inCell && alone);
    TEST_CHECK(subset && !west.empty() && west.size() < list.size());
    TEST_CHECK(list == entrancesOver(CaveField(kSeed), ground, kHalf));
    TEST_CHECK(list != entrancesOver(CaveField(kSeed + 1), ground, kHalf));
}

// Top over the ground of the site, bottom at the depth and the run of the parameters, ground
// along the run above the shore, chamber under the ground and within the maximum depth.
void testGeometry() {
    const Heightfield heightfield(kSeed);
    const CaveGround ground = heightfieldGround(heightfield);
    const CaveField field(kSeed);
    const CaveParams& p = field.params();
    const terrain::EntranceParams& q = p.entrances;
    constexpr double kSlack = 1.0e-3;
    bool top = true;
    bool bottom = true;
    bool radii = true;
    bool shore = true;
    bool chamber = true;
    for (const CaveEntrance& e : entrancesOver(field, ground, kHalf)) {
        const double site = static_cast<double>(ground.height(e.topX, e.topZ));
        const double depth = site - e.bottomY;
        const double run = std::hypot(e.bottomX - e.topX, e.bottomZ - e.topZ);
        top = top && e.topY == site + static_cast<double>(q.aboveGround);
        bottom = bottom && depth >= q.minDepth - kSlack && depth <= q.maxDepth + kSlack &&
                 run >= q.minRun - kSlack && run <= q.maxRun + kSlack;
        radii = radii && e.tubeRadius >= q.minTubeRadius && e.tubeRadius <= q.maxTubeRadius &&
                e.chamberRadius >= q.minChamberRadius && e.chamberRadius <= q.maxChamberRadius;
        for (const double t : {0.0, 0.25, 0.5, 0.75, 1.0}) {
            const auto [x, z] = axisAt(e, t);
            shore = shore && ground.height(x, z) >= terrain::kSeaLevel + q.minShoreHeight;
        }
        const double covered = static_cast<double>(ground.height(e.bottomX, e.bottomZ)) - e.bottomY;
        chamber = chamber && covered + e.chamberRadius <= p.maxDepth &&
                  covered - e.chamberRadius >= p.minRoof / 2.0;
    }
    TEST_CHECK(top);
    TEST_CHECK(bottom);
    TEST_CHECK(radii);
    TEST_CHECK(shore);
    TEST_CHECK(chamber);
}

// Distance to the axis seen from above: 0 on the axis, the offset beside it and beyond its ends.
bool axisDistanceIsExact(const CaveEntrance& e) {
    const double run = std::hypot(e.bottomX - e.topX, e.bottomZ - e.topZ);
    const double alongX = (e.bottomX - e.topX) / run;
    const double alongZ = (e.bottomZ - e.topZ) / run;
    const auto [midX, midZ] = axisAt(e, 0.5);
    const auto within = [&e](double x, double z, double expected) {
        return std::abs(entranceAxisDistance(e, x, z) - expected) < 1.0e-9;
    };
    return within(e.topX, e.topZ, 0.0) && within(e.bottomX, e.bottomZ, 0.0) &&
           within(midX, midZ, 0.0) && within(midX - 7.0 * alongZ, midZ + 7.0 * alongX, 7.0) &&
           within(e.bottomX + 9.0 * alongX, e.bottomZ + 9.0 * alongZ, 9.0) &&
           within(e.topX - 5.0 * alongX, e.topZ - 5.0 * alongZ, 5.0);
}

// Void of the tube and the chamber on the axis: -radius at the top, -chamberRadius at the
// bottom, at most -tubeRadius along the tube, air just over the chamber's pole.
bool voidOnAxisIsExact(const CaveEntrance& e) {
    const auto [midX, midZ] = axisAt(e, 0.5);
    const float tolerance = 1.0e-4f;
    return std::abs(entranceVoidAt(e, e.bottomX, e.bottomY, e.bottomZ) + e.chamberRadius) <
               tolerance &&
           std::abs(entranceVoidAt(e, e.topX, e.topY, e.topZ) + e.tubeRadius) < tolerance &&
           entranceVoidAt(e, midX, (e.topY + e.bottomY) / 2.0, midZ) <=
               -e.tubeRadius + tolerance &&
           entranceVoidAt(e, e.bottomX, e.bottomY + e.chamberRadius + 2.0, e.bottomZ) > 0.0f;
}

struct AroundResult {
    bool lipschitz = true;
    bool farAway = true;
    bool reach = true;
};

// Random points around the entrance: the void is 1-Lipschitz, at least the reach margin beyond
// the reach, and a column holds the entrance exactly within its reach.
AroundResult voidAround(const CaveField& field, const CaveGround& ground, const CaveEntrance& e,
                        Rng& rng) {
    AroundResult result;
    const double margin = terrain::kEntranceReachMargin;
    for (int k = 0; k < 300; ++k) {
        const Point a = pointNearEntrance(ground, rng, e, 40.0);
        const Point b = pointNearEntrance(ground, rng, e, 40.0);
        const double va = static_cast<double>(entranceVoidAt(e, a.x, a.y, a.z));
        const double vb = static_cast<double>(entranceVoidAt(e, b.x, b.y, b.z));
        const double distance = std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
        result.lipschitz = result.lipschitz && std::abs(va - vb) <= distance + 1.0e-3;
        const bool outside = entranceAxisDistance(e, a.x, a.z) >= field.entranceReach(e);
        result.farAway = result.farAway && (!outside || va >= margin - 1.0e-3);
        const CaveColumn column = field.column(a.x, a.z, a.ground, ground, std::span(&e, 1));
        result.reach = result.reach && column.entranceCount == (outside ? 0 : 1);
    }
    return result;
}

// Geometry of the void of the entrances: on the axis, around it and at its reach.
void testVoidGeometry() {
    const Heightfield heightfield(kSeed);
    const CaveGround ground = heightfieldGround(heightfield);
    const CaveField field(kSeed);
    const std::vector<CaveEntrance> list = entrancesOver(field, ground, kHalf);
    Rng rng(4);
    bool axis = true;
    bool voids = true;
    AroundResult around;
    const std::size_t count = std::min<std::size_t>(list.size(), 12);
    for (std::size_t n = 0; n < count; ++n) {
        axis = axis && axisDistanceIsExact(list[n]);
        voids = voids && voidOnAxisIsExact(list[n]);
        const AroundResult result = voidAround(field, ground, list[n], rng);
        around.lipschitz = around.lipschitz && result.lipschitz;
        around.farAway = around.farAway && result.farAway;
        around.reach = around.reach && result.reach;
    }
    TEST_CHECK(count == 12);
    TEST_CHECK(axis && voids);
    TEST_CHECK(around.lipschitz && around.farAway && around.reach);
}

// Box of the tube and the chamber of an entrance, 3 m larger than them in every direction.
Lattice latticeAround(const CaveEntrance& e) {
    const double margin = largestRadius(e) + 3.0;
    const auto low = [](double v) { return static_cast<int>(std::floor(v)); };
    const auto high = [](double v) { return static_cast<int>(std::ceil(v)); };
    const int x0 = low(std::min(e.topX, e.bottomX) - margin);
    const int z0 = low(std::min(e.topZ, e.bottomZ) - margin);
    const int y0 = low(e.bottomY - margin);
    return {x0, y0, z0, high(std::max(e.topX, e.bottomX) + margin) - x0 + 1,
            high(e.topY + margin) - y0 + 1, high(std::max(e.topZ, e.bottomZ) + margin) - z0 + 1};
}

// From the sky, a continuous path of air leads down the tube to the chamber, deep underground.
void testAirPathFromSky() {
    const Heightfield heightfield(kSeed);
    const CaveGround ground = heightfieldGround(heightfield);
    const CaveField field(kSeed);
    const std::vector<CaveEntrance> list = entrancesOver(field, ground, kHalf);
    constexpr std::size_t kChecked = 8;
    TEST_CHECK(list.size() >= kChecked);
    for (std::size_t n = 0; n < kChecked && n < list.size(); ++n) {
        const CaveEntrance& e = list[n * list.size() / kChecked];
        SkyFill fill(field, ground, latticeAround(e));
        fill.run();
        double deepest = 0.0;
        std::size_t underground = 0;
        fill.undergroundExtent(deepest, underground);
        std::printf("air path: entrance of cell (%d, %d), %zu samples, %zu reached under the "
                    "ground, down to %.1f m\n",
                    e.cellX, e.cellZ, fill.samples(), underground, deepest);
        TEST_CHECK(fill.reached(e.bottomX, e.bottomY, e.bottomZ));
        for (const double t : {0.0, 0.25, 0.5, 0.75}) {
            const auto [x, z] = axisAt(e, t);
            TEST_CHECK(fill.reached(x, e.topY + t * (e.bottomY - e.topY), z));
        }
        TEST_CHECK(deepest >= static_cast<double>(field.params().minRoof));
        TEST_CHECK(underground > 500);
    }
}

// No entrance on flat ground in the sea or on the shore, even over a large area; some just
// above the shore height.
void testNoEntranceOnTheShore() {
    const CaveField field(kSeed);
    const float shore = terrain::kSeaLevel + field.params().entrances.minShoreHeight;
    for (const float height : {terrain::kSeaLevel + 2.0f, terrain::kSeaLevel, -50.0f,
                               shore - 0.01f}) {
        TEST_CHECK(entrancesOver(field, flatGround(height), kHalf).empty());
    }
    TEST_CHECK(!entrancesOver(field, flatGround(shore), kHalf).empty());
    TEST_CHECK(!entrancesOver(field, flatGround(shore + 100.0f), kHalf).empty());
}

// Smallest distance to the river line x = riverX along the axis of the entrance.
double nearestToRiver(const CaveEntrance& e, double riverX) {
    double nearest = 1.0e30;
    for (int i = 0; i <= 20; ++i) {
        nearest = std::min(nearest, std::abs(axisAt(e, i / 20.0).first - riverX));
    }
    return nearest;
}

// A straight river along x = riverX removes the entrances that come near it, and only those;
// the others are the entrances of the same ground without river.
void checkRiver(double riverX, std::size_t minDropped) {
    const CaveField field(kSeed);
    const CaveParams& p = field.params();
    const CaveGround dry = flatGround(100.0f);
    const CaveGround wet = withStraightRiver(dry, riverX);
    const std::vector<CaveEntrance> kept = entrancesOver(field, wet, 8000.0);
    const std::vector<CaveEntrance> all = entrancesOver(field, dry, 8000.0);
    const auto clearance = [&p](const CaveEntrance& e) {
        return static_cast<double>(p.riverClearance) + largestRadius(e);
    };
    bool apart = true;
    bool sameSide = true;
    for (const CaveEntrance& e : kept) {
        apart = apart && nearestToRiver(e, riverX) > clearance(e);
        sameSide = sameSide && (e.topX - riverX) * (e.bottomX - riverX) > 0.0;
    }
    std::size_t next = 0;
    std::size_t dropped = 0;
    bool identical = true;
    bool droppedNear = true;
    for (const CaveEntrance& e : all) {
        if (next < kept.size() && kept[next].cellX == e.cellX && kept[next].cellZ == e.cellZ) {
            identical = identical && kept[next] == e;
            ++next;
        } else {
            ++dropped;
            droppedNear = droppedNear && nearestToRiver(e, riverX) <
                                             clearance(e) + static_cast<double>(p.fade) + 1.0e-6;
        }
    }
    std::printf("river at x = %.0f: %zu entrances kept, %zu dropped near the river\n", riverX,
                kept.size(), dropped);
    TEST_CHECK(kept.size() >= 50);
    TEST_CHECK(apart && sameSide);
    TEST_CHECK(next == kept.size() && identical);
    TEST_CHECK(dropped >= minDropped && droppedNear);
}

// Rivers keep the entrances away: one along the gap between two columns of cells, whose sites
// stay 25 m from it, and one through the middle of a column.
void testNoEntranceNearRiver() {
    checkRiver(0.0, 0);
    checkRiver(128.0, 3);
}

// Calls visit(x, depth, terrain, carved) for the samples of the box around the entrance (1 m
// apart, z every 2 m), with the density that the entrance alone carves out of ground.
template <typename Visit>
void scanEntrance(const CaveField& field, const CaveGround& ground, const CaveEntrance& e,
                  Visit visit) {
    const CaveDetail detail{terrain::kCaveEntrances, 4.0f};
    const std::span<const CaveEntrance> list(&e, 1);
    for (int x = -14; x <= 44; ++x) {
        for (int z = -8; z <= 8; z += 2) {
            const float height = ground.height(x, z);
            const CaveColumn column = field.column(x, z, height, ground, list);
            for (int y = -40; y <= 110; ++y) {
                const float rock = terrainDensity(y, height);
                const float caveVoid = field.combine(
                    field.voids(column, y, terrain::CaveNoiseValues{}, list, detail));
                const float carved = field.carve(rock, caveVoid, detail);
                visit(x, static_cast<double>(height) - y, rock, carved);
            }
        }
    }
}

// An entrance ignores the roof only: its tube opens through it, but it stops at the maximum
// depth and keeps clear of the rivers like every cave.
void testEntranceLimits() {
    const CaveField field(kSeed);
    const CaveParams& p = field.params();
    CaveEntrance e;
    e.topX = 0.0;
    e.topY = 103.0;
    e.bottomX = 30.0;
    e.bottomY = -20.0; // 120 m under the ground, deeper than the caves go
    e.tubeRadius = 4.0f;
    e.chamberRadius = 9.0f;
    const double roof = static_cast<double>(p.minRoof);
    const double deepest = static_cast<double>(p.maxDepth + p.blend / 4.0f);
    int throughRoof = 0;
    int nearBottom = 0;
    int tooDeep = 0;
    scanEntrance(field, flatGround(100.0f), e, [&](int, double depth, float rock, float carved) {
        const bool air = carvedAir(rock, carved);
        throughRoof += air && depth > 0.0 && depth < roof ? 1 : 0;
        nearBottom += air && depth > static_cast<double>(p.maxDepth) - 20.0 ? 1 : 0;
        tooDeep += air && depth > deepest ? 1 : 0;
    });
    // The same entrance beside a river along x = 15, which the whole entrance lies within.
    const double clearance = static_cast<double>(p.riverClearance);
    int nearRiver = 0;
    bool untouched = true;
    scanEntrance(field, withStraightRiver(flatGround(100.0f), 15.0), e,
                 [&](int x, double, float rock, float carved) {
                     const double distance = std::abs(x - 15.0);
                     nearRiver += distance <= clearance - 1.0 && carvedAir(rock, carved) ? 1 : 0;
                     untouched = untouched && (distance > clearance - 6.0 || carved == rock);
                 });
    std::printf("entrance limits: %d air samples through the roof, %d near the maximum depth\n",
                throughRoof, nearBottom);
    TEST_CHECK(throughRoof > 100);
    TEST_CHECK(nearBottom > 100);
    TEST_CHECK(tooDeep == 0);
    TEST_CHECK(nearRiver == 0 && untouched);
}

// On flat ground one cell in `chance` has an entrance.
void testChance() {
    const CaveField field(kSeed);
    const CaveGround ground = flatGround(100.0f);
    int found = 0;
    constexpr int kCells = 60;
    for (int cellZ = -kCells; cellZ < kCells; ++cellZ) {
        for (int cellX = -kCells; cellX < kCells; ++cellX) {
            CaveEntrance e;
            found += field.entrance(cellX, cellZ, ground, e) ? 1 : 0;
        }
    }
    const double share = found / (4.0 * kCells * kCells);
    std::printf("chance: %.3f of the cells have an entrance (chance %.2f)\n", share,
                static_cast<double>(field.params().entrances.chance));
    TEST_CHECK(std::abs(share - static_cast<double>(field.params().entrances.chance)) < 0.03);
}

} // namespace

int main() {
    testEntrancesExist();
    testGeometry();
    testVoidGeometry();
    testAirPathFromSky();
    testNoEntranceOnTheShore();
    testNoEntranceNearRiver();
    testEntranceLimits();
    testChance();
    return test::result();
}

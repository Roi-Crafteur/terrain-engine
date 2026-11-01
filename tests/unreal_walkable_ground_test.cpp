// Where the pawn of the game is put down when the tools give it back (Bridge/walkable_ground.hpp,
// ATerrainWorld::ReturnToGamePawn): the walkable ground nearest to the column under the view, on
// made-up reliefs (a flat ground, terraces in a wall of slope 3.28, water), then under the wall
// of the K2 of the default world (k2_paroi, without the planet file); the reasons when there is
// none; and the check of a candidate on the collision of the tiles (too steep, a step, far from
// the heightmap as in the basin of the fall of 388 m, under water).
#include "walkable_ground.hpp"

#include "test_assert.hpp"

#include "terrain/generation/heightfield.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

namespace {

using terrain::unreal::GroundHeight;
using terrain::unreal::GroundWet;
using terrain::unreal::nearestWalkable;
using terrain::unreal::WalkableSearch;

// The walkable angle of a CharacterMovement (44.765 degrees) less the margin of the tools (5).
WalkableSearch defaultSearch() {
    WalkableSearch search;
    search.maxSlope = terrain::unreal::walkableSlope(44.765, 5.0);
    return search;
}

const GroundWet kDry = [](double, double) { return false; };

// A wall of slope 3.28 (73 degrees, the slope under k2_paroi) rising to the east, with flat
// terraces where x lies in [from, to].
GroundHeight wallWithTerraces(double from, double to, double from2 = 1e9, double to2 = 1e9) {
    return [=](double x, double) {
        if (x >= from && x <= to) {
            return 3.28 * from;
        }
        if (x >= from2 && x <= to2) {
            return 3.28 * from2;
        }
        return 3.28 * x;
    };
}

void testSlopes() {
    const double max = terrain::unreal::walkableSlope(44.765, 5.0);
    TEST_CHECK(std::abs(max - std::tan(39.765 * 3.14159265358979323846 / 180.0)) < 1e-12);
    TEST_CHECK(terrain::unreal::walkableSlope(3.0, 5.0) == 0.0);
    const GroundHeight wall = wallWithTerraces(1e8, 1e8);
    TEST_CHECK(std::abs(terrain::unreal::groundSlope(5.0, 7.0, 1.0, wall) - 3.28) < 1e-9);
    const GroundHeight flat = [](double, double) { return 120.0; };
    TEST_CHECK(terrain::unreal::groundSlope(5.0, 7.0, 1.0, flat) == 0.0);
}

void testMadeUpReliefs() {
    const WalkableSearch search = defaultSearch();
    // A flat ground: the column itself.
    const GroundHeight flat = [](double, double) { return 120.0; };
    const auto here = nearestWalkable(5.0, 7.0, search, flat, kDry);
    TEST_CHECK(here && here->x == 5.0 && here->z == 7.0 && here->distance == 0.0);
    TEST_CHECK(here && here->ground == 120.0 && here->slope == 0.0);
    // The wall with a terrace 30 m to the east: the terrace, on the ring of 30 m.
    const GroundHeight closeTerrace = wallWithTerraces(28.0, 34.0);
    const auto terrace = nearestWalkable(0.0, 0.0, search, closeTerrace, kDry);
    TEST_CHECK(terrace && terrace->distance == 30.0 && terrace->slope == 0.0);
    TEST_CHECK(terrace && terrace->x >= 29.0 && terrace->x <= 33.0);
    TEST_CHECK(terrace && terrace->ground == 3.28 * 28.0);
    // The same search again: the same spot, bit for bit.
    const auto again = nearestWalkable(0.0, 0.0, search, closeTerrace, kDry);
    TEST_CHECK(again && terrace &&
               std::bit_cast<std::uint64_t>(again->x) == std::bit_cast<std::uint64_t>(terrace->x) &&
               std::bit_cast<std::uint64_t>(again->z) == std::bit_cast<std::uint64_t>(terrace->z));
    // A terrace 60 m away only: none within 50 m (the pawn is not put down).
    TEST_CHECK(!nearestWalkable(0.0, 0.0, search, wallWithTerraces(58.0, 64.0), kDry));
    WalkableSearch wider = search;
    wider.radius = 70.0;
    TEST_CHECK(nearestWalkable(0.0, 0.0, wider, wallWithTerraces(58.0, 64.0), kDry).has_value());
    // The near terrace under water: skipped, the dry one 40 m to the west instead.
    const GroundHeight two = wallWithTerraces(28.0, 34.0, -44.0, -38.0);
    const GroundWet lake = [](double x, double) { return x >= 28.0 && x <= 34.0; };
    const auto dry = nearestWalkable(0.0, 0.0, search, two, lake);
    TEST_CHECK(dry && dry->distance == 40.0 && dry->x < -38.0 && dry->slope == 0.0);
    TEST_CHECK(!nearestWalkable(0.0, 0.0, search, closeTerrace, lake));
    // A search without rings finds nothing rather than looping.
    WalkableSearch broken = search;
    broken.ringStep = 0.0;
    TEST_CHECK(!nearestWalkable(5.0, 7.0, broken, flat, kDry));
}

// Under the highest wall near the K2 (k2_paroi, 416102 ; -11414891, without the planet file):
// the column slopes at 72 degrees (the pawn slid 200 m down from it); the walkable ground
// nearest to it lies 34 m away, at 6,645.75 m, on a slope of 39 degrees.
void testUnderTheK2() {
    const terrain::Heightfield field(terrain::kDefaultWorldSeed);
    const GroundHeight height = [&field](double x, double z) {
        return static_cast<double>(field.heightAt(x, z));
    };
    const GroundWet wet = [&field](double x, double z) {
        const terrain::HeightSample s = field.sampleAt(x, z);
        return s.water != terrain::WaterKind::None && s.waterLevel > s.height;
    };
    const double x = 416102.0;
    const double z = -11414891.0;
    const WalkableSearch search = defaultSearch();
    TEST_CHECK(terrain::unreal::groundSlope(x, z, 1.0, height) > 3.0);
    const auto spot = nearestWalkable(x, z, search, height, wet);
    TEST_CHECK(spot.has_value());
    if (spot) {
        TEST_CHECK(spot->distance == 34.0 && spot->slope <= search.maxSlope);
        TEST_CHECK(std::abs(spot->x - 416125.506) < 0.01);
        TEST_CHECK(std::abs(spot->z + 11414915.565) < 0.01);
        TEST_CHECK(std::abs(spot->ground - 6645.75) < 0.01);
        TEST_CHECK(std::abs(terrain::unreal::groundSlope(spot->x, spot->z, 1.0, height) -
                            spot->slope) == 0.0);
    }
}

} // namespace

// Every walkable point, nearest ring first and the gentlest first on a ring, and why there is none.
void testCandidates() {
    using terrain::unreal::WalkableReason;
    const WalkableSearch search = defaultSearch();
    const GroundHeight flat = [](double, double) { return 120.0; };
    const auto all = terrain::unreal::walkableCandidates(0.0, 0.0, search, flat, kDry, 10);
    TEST_CHECK(all.reason == WalkableReason::Found && all.spots.size() == 10);
    TEST_CHECK(all.spots[0].distance == 0.0); // the column, then the 8 points of the ring of 2 m
    for (std::size_t i = 1; i <= 8; ++i) {
        TEST_CHECK(all.spots[i].distance == 2.0);
    }
    TEST_CHECK(all.spots[9].distance == 4.0);
    // On a ring, the gentlest first: a ground that tilts more and more toward the east.
    const GroundHeight bowl = [](double x, double) { return x > 0.0 ? 0.2 * x * x : 0.0; };
    const auto ring = terrain::unreal::walkableCandidates(0.0, 0.0, search, bowl, kDry, 64);
    for (std::size_t i = 1; i < ring.spots.size(); ++i) {
        const auto& a = ring.spots[i - 1];
        const auto& b = ring.spots[i];
        TEST_CHECK(a.distance < b.distance || (a.distance == b.distance && a.slope <= b.slope));
    }
    // A view over water: no dry ground (agent 35 poses the flying view over the water).
    const GroundWet sea = [](double, double) { return true; };
    const auto wet = terrain::unreal::walkableCandidates(0.0, 0.0, search, flat, sea, 10);
    TEST_CHECK(wet.spots.empty() && wet.reason == WalkableReason::NoDryGround);
    TEST_CHECK(std::string(terrain::unreal::walkableReasonText(wet.reason)).find("no dry ground") !=
               std::string::npos);
    // Dry ground, all of it too steep.
    const GroundHeight wall = wallWithTerraces(1e8, 1e8);
    const auto steep = terrain::unreal::walkableCandidates(0.0, 0.0, search, wall, kDry, 10);
    TEST_CHECK(steep.spots.empty() && steep.reason == WalkableReason::TooSteep);
    WalkableSearch broken = search;
    broken.probe = 0.0;
    TEST_CHECK(terrain::unreal::walkableCandidates(0.0, 0.0, broken, flat, kDry, 10).reason ==
               WalkableReason::BadSearch);
}

// The water surface of the core around a point: the lake 1 m away seen from a dry bank.
void testWaterAround() {
    const terrain::unreal::GroundSample lakeWest = [](double x, double) {
        terrain::HeightSample s;
        s.height = x < 0.0 ? 178.0f : 181.0f;
        s.water = x < 0.0 ? terrain::WaterKind::Lake : terrain::WaterKind::None;
        s.waterLevel = x < 0.0 ? 180.03f : s.height;
        return s;
    };
    const auto surface = terrain::unreal::waterSurfaceAround(2.0, 0.0, 3.0, lakeWest);
    TEST_CHECK(surface && *surface == static_cast<double>(180.03f));
    TEST_CHECK(!terrain::unreal::waterSurfaceAround(10.0, 0.0, 3.0, lakeWest));
}

// A candidate checked on the collision of the tiles: five rays, the center first.
void testStand() {
    using terrain::unreal::FloorProbe;
    using terrain::unreal::StandRefusal;
    terrain::unreal::StandLimits limits;
    limits.minNormalUp = terrain::unreal::walkableNormalUp(44.765, 5.0);
    TEST_CHECK(std::abs(limits.minNormalUp - std::cos(39.765 * 3.14159265358979323846 / 180.0)) <
               1e-12);
    std::array<FloorProbe, 5> probes{};
    for (FloorProbe& p : probes) {
        p = {true, 100.0, 1.0};
    }
    probes[2].altitude = 100.2;
    const auto stands = terrain::unreal::checkStand(probes, 100.5, std::nullopt, limits);
    TEST_CHECK(stands.refusal == StandRefusal::None && stands.altitude == 100.2);
    // A normal at 50 degrees: too steep for 39.8.
    auto steep = probes;
    steep[3].normalUp = std::cos(50.0 * 3.14159265358979323846 / 180.0);
    TEST_CHECK(terrain::unreal::checkStand(steep, 100.0, std::nullopt, limits).refusal ==
               StandRefusal::TooSteep);
    // A ray that hits nothing: no collision there yet.
    auto missing = probes;
    missing[4].hit = false;
    TEST_CHECK(terrain::unreal::checkStand(missing, 100.0, std::nullopt, limits).refusal ==
               StandRefusal::NoCollision);
    // A step of 60 cm under the capsule.
    auto step = probes;
    step[1].altitude = 100.6;
    TEST_CHECK(terrain::unreal::checkStand(step, 100.0, std::nullopt, limits).refusal ==
               StandRefusal::Step);
    // The basin of the fall of 388 m (-6185124 ; 155984): the heightmap says dry ground at
    // 260.11 m, the collision of the tiles lies at the bottom of the basin, under its water at
    // 180.03 m: far from the heightmap, and under water.
    std::array<FloorProbe, 5> basin{};
    for (FloorProbe& p : basin) {
        p = {true, 178.5, 0.99};
    }
    const std::optional<double> water = 180.03;
    TEST_CHECK(terrain::unreal::checkStand(basin, 260.11, water, limits).refusal ==
               StandRefusal::OffHeightmap);
    TEST_CHECK(terrain::unreal::checkStand(basin, 179.0, water, limits).refusal ==
               StandRefusal::UnderWater);
    TEST_CHECK(terrain::unreal::checkStand(basin, 179.0, 178.0, limits).refusal ==
               StandRefusal::None);
    TEST_CHECK(std::string(terrain::unreal::standRefusalText(StandRefusal::UnderWater)) ==
               "its collision is under the water of the core");
}

int main() {
    testSlopes();
    testMadeUpReliefs();
    testUnderTheK2();
    testCandidates();
    testWaterAround();
    testStand();
    return test::result();
}

// Where the pawn of the game is put down when the tools give it back (Bridge/walkable_ground.hpp,
// ATerrainWorld::ReturnToGamePawn): the walkable ground nearest to the column under the view, on
// made-up reliefs (a flat ground, terraces in a wall of slope 3.28, water), then under the wall
// of the K2 of the default world (k2_paroi, without the planet file).
#include "walkable_ground.hpp"

#include "test_assert.hpp"

#include "terrain/generation/heightfield.hpp"

#include <bit>
#include <cmath>
#include <cstdint>

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

int main() {
    testSlopes();
    testMadeUpReliefs();
    testUnderTheK2();
    return test::result();
}

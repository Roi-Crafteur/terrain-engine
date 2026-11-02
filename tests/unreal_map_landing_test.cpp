// Where a click on the dev map puts the player (Bridge/map_landing.hpp): on made-up columns of the
// core (a dry plain, a shallow shore, the open sea, the middle of a lake, a dry wall, a cliff at the
// edge of a lake), the decision between the pawn of the game ("land", the spot then chosen and
// checked by ReturnToGamePawn of agent 36) and the view of the tools above the water, the view
// after a refusal of ReturnToGamePawn, and the eye of a view over water: 1.8 m above the surface
// of the water, never above the bottom.
#include "map_landing.hpp"
#include "map_targets.hpp"

#include "test_assert.hpp"

#include "terrain/generation/heightfield.hpp"

#include <cmath>
#include <functional>
#include <string>

using namespace terrain;
using namespace terrain::unreal;

namespace {

constexpr double kRadius = 50.0; // Terrain.PawnRadius by default

// A made-up column of the core at (x, z).
using Columns = std::function<HeightSample(double x, double z)>;

HeightSample dryAt(double height) {
    HeightSample s;
    s.height = static_cast<float>(height);
    s.water = WaterKind::None;
    s.waterLevel = s.height;
    return s;
}

HeightSample waterAt(double bottom, double level, WaterKind kind) {
    HeightSample s;
    s.height = static_cast<float>(bottom);
    s.water = kind;
    s.waterLevel = static_cast<float>(level);
    return s;
}

// clickLanding over made-up columns, as clickLandingOn reads a heightfield.
MapLanding landingOver(const Columns& columns, double x, double z) {
    const GroundHeight height = [&columns](double px, double pz) {
        return static_cast<double>(columns(px, pz).height);
    };
    const GroundWet wet = [&columns](double px, double pz) {
        return columnHasWater(columns(px, pz));
    };
    return clickLanding(x, z, kRadius, height, wet);
}

// The eye of TeleportView(x, z, ..., kMapEyeAboveGround, Ground): 1.8 m above the top of the
// column, its water when it holds some.
double eyeOver(const Columns& columns, double x, double z) {
    return eyeAltitude(kMapEyeAboveGround, dev::EyeMode::Ground, columnTop(columns(x, z)));
}

void testDryPlain() {
    const Columns plain = [](double, double) { return dryAt(12.0); };
    const MapLanding l = landingOver(plain, 100.0, -40.0);
    TEST_CHECK(l.kind == MapLandingKind::Land && !l.clickWet);
    TEST_CHECK(l.x == 100.0 && l.z == -40.0 && l.clickX == 100.0 && l.clickZ == -40.0);
    TEST_CHECK(!landingFallback(l).has_value()); // a dry click: the view of the tools stays
}

// The sea west of x = 30, 1 m deep; the beach east of it.
void testShallowShore() {
    const Columns shore = [](double x, double) {
        return x < 30.0 ? waterAt(-1.0, 0.0, WaterKind::Ocean) : dryAt(0.5);
    };
    const MapLanding l = landingOver(shore, 0.0, 0.0);
    TEST_CHECK(l.kind == MapLandingKind::Land && l.clickWet);
    TEST_CHECK(l.x >= 30.0 && std::hypot(l.x, l.z) <= 32.0 + 1e-9); // the ring of 32 m
    TEST_CHECK(l.reason == WalkableReason::Found);
    TEST_CHECK(!columnHasWater(shore(l.x, l.z)));
    // ReturnToGamePawn refused every spot near the beach: back above the sea at the click.
    const std::optional<MapLanding> back = landingFallback(l);
    TEST_CHECK(back && back->kind == MapLandingKind::OverWater && back->x == 0.0 && back->z == 0.0);
}

void testOpenSea() {
    const Columns sea = [](double, double) { return waterAt(-800.0, 0.0, WaterKind::Ocean); };
    const MapLanding l = landingOver(sea, 1000.0, 2000.0);
    TEST_CHECK(l.kind == MapLandingKind::OverWater && l.clickWet);
    TEST_CHECK(l.reason == WalkableReason::NoDryGround);
    TEST_CHECK(l.x == 1000.0 && l.z == 2000.0);
    TEST_CHECK(eyeOver(sea, l.x, l.z) == 1.8); // above the sea, not 798.2 m under it
    TEST_CHECK(!landingFallback(l).has_value());
    TEST_CHECK(landingText(l, kRadius).find("no dry ground within 50 m") != std::string::npos);
}

// A mountain lake (its water at 1725.26 m, 30 m deep) of radius 80 m: dry ground 80 m from the
// click, beyond the 50 m of the search.
void testMiddleOfALake() {
    const Columns lake = [](double x, double z) {
        return std::hypot(x, z) < 80.0 ? waterAt(1695.26, 1725.26, WaterKind::Lake)
                                       : dryAt(1726.0);
    };
    const MapLanding l = landingOver(lake, 0.0, 0.0);
    TEST_CHECK(l.kind == MapLandingKind::OverWater && l.reason == WalkableReason::NoDryGround);
    TEST_CHECK(std::abs(eyeOver(lake, 0.0, 0.0) - (1725.26f + 1.8)) < 1e-9); // never the bottom
    // 40 m from the shore, the dry ground is in the radius: the pawn of the game.
    const MapLanding nearShore = landingOver(lake, 40.0, 0.0);
    TEST_CHECK(nearShore.kind == MapLandingKind::Land && nearShore.clickWet);
    TEST_CHECK(std::hypot(nearShore.x, nearShore.z) >= 80.0);
}

// A dry wall of 84 degrees clicked: land at the click, ReturnToGamePawn looks for its spot.
void testDryWall() {
    const Columns wall = [](double x, double) { return dryAt(10.0 * x); };
    const MapLanding l = landingOver(wall, 5.0, 5.0);
    TEST_CHECK(l.kind == MapLandingKind::Land && !l.clickWet && l.x == 5.0 && l.z == 5.0);
    TEST_CHECK(!landingFallback(l).has_value());
}

// The foot of a cliff at the edge of a lake: the lake west of x = 20 (its water at 3 m), a dry
// wall of 88 degrees east of it. Clicked from the lake: land on the wall (any slope: the nearest
// dry column), then, once ReturnToGamePawn refused all its spots, the view back above the lake at
// the click, never left against the wall.
void testCliffAtALake() {
    const Columns cliff = [](double x, double) {
        return x < 20.0 ? waterAt(-4.0, 3.0, WaterKind::Lake) : dryAt(3.5 + 30.0 * (x - 20.0));
    };
    const MapLanding l = landingOver(cliff, 0.0, 0.0);
    TEST_CHECK(l.kind == MapLandingKind::Land && l.clickWet && l.x >= 20.0);
    const std::optional<MapLanding> back = landingFallback(l);
    TEST_CHECK(back && back->kind == MapLandingKind::OverWater);
    TEST_CHECK(back->x == 0.0 && back->z == 0.0);
    TEST_CHECK(eyeOver(cliff, back->x, back->z) == 4.8); // the lake + 1.8, not the bottom + 1.8
    TEST_CHECK(landingText(*back, kRadius).find("over water") == 0);
}

// The world of the default seed, without the planet file: the beach of the origin (plage_origine)
// is dry ground, the sea west of it has no dry ground within 50 m.
void testDefaultWorld() {
    const Heightfield field(kDefaultWorldSeed);
    const MapLanding beach = clickLandingOn(field, -3985.0, -165.0, kRadius);
    TEST_CHECK(beach.kind == MapLandingKind::Land);
    TEST_CHECK(columnHasWater(field.sampleAt(beach.x, beach.z)) == false);
}

} // namespace

int main() {
    testDryPlain();
    testShallowShore();
    testOpenSea();
    testMiddleOfALake();
    testDryWall();
    testCliffAtALake();
    testDefaultWorld();
    return test::result();
}

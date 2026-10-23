// The sky over the far horizon (Bridge/horizon_atmosphere): why a SkyAtmosphere left at the
// origin turns black far from it, its planet placed under the camera (the altitude it sees is the
// altitude above the sea, anywhere on the planet, across the origin rebasing), the curved sea of
// the horizon on its ground, the depth of its aerial perspective, and the origin of the waves of
// the sea (in place across a world turn and the origin rebasing).
#include "horizon_atmosphere.hpp"

#include "horizon_layout.hpp"
#include "test_assert.hpp"

#include "terrain/streaming/horizon_levels.hpp"
#include "terrain/world_size.hpp"

#include <array>
#include <cmath>

namespace {

using terrain::unreal::UnrealFrame;
using terrain::unreal::UnrealPoint;

constexpr double kRadius = 60'030'976.0 / (2.0 * 3.14159265358979323846); // 9,554 km
constexpr double kCm = 100.0;

// The frame of the test level: the ground of (22960, -4060) at 2,709.3 m on the Unreal origin.
UnrealFrame gaspFrame() {
    return {{22960.0, 2709.3, -4060.0}};
}

// A SkyAtmosphere left with its planet top at the sea level under the Unreal origin: the camera
// stands higher and higher above its ground as it goes away, in space beyond about 1,100 km
// (its air ends 60 to 100 km up): the black sky.
void testFixedPlanetGoesBlack() {
    const UnrealFrame frame = gaspFrame();
    const UnrealPoint top = terrain::unreal::planetTopUnder({0.0, 0.0, 0.0}, frame);
    const double sea = terrain::unreal::seaLevelZ(frame);
    TEST_CHECK(sea == -270930.0 || std::abs(sea + 270930.0) < 1e-6);
    const UnrealPoint near{0.0, 0.0, sea + 2.0 * kCm};
    TEST_CHECK(std::abs(terrain::unreal::heightAbovePlanet(near, top, kRadius) - 2.0) < 1e-6);
    // 5,000 km away on the beach: 1,229 km "up" for the atmosphere.
    const UnrealPoint far{5.0e6 * kCm, 0.0, sea + 2.0 * kCm};
    const double height = terrain::unreal::heightAbovePlanet(far, top, kRadius);
    TEST_CHECK(height > 1.2e6 && height < 1.25e6);
    // Half a world away, beyond the planet itself.
    const UnrealPoint half{30.015488e6 * kCm, 0.0, sea};
    TEST_CHECK(terrain::unreal::heightAbovePlanet(half, top, kRadius) > 2.0e7);
}

// The planet under the camera: the atmosphere sees the altitude above the sea, wherever the
// camera stands in the Unreal world, from the sea level to 9 km.
void testPlanetUnderCamera() {
    const UnrealFrame frame = gaspFrame();
    const double sea = terrain::unreal::seaLevelZ(frame);
    const std::array<UnrealPoint, 5> places{{{0.0, 0.0, 0.0},
                                             {5.2e8, 9.0e6, 0.0},
                                             {1.511e9, -4.48e7, 0.0},
                                             {3.0043e9, 1.5e7, 0.0},
                                             {-4.5e9, 2.6e9, 0.0}}};
    for (const UnrealPoint& place : places) {
        for (const double altitude : {0.0, 1.8, 4000.0, 9000.0}) {
            const UnrealPoint camera{place.x, place.y, sea + altitude * kCm};
            const UnrealPoint top = terrain::unreal::planetTopUnder(camera, frame);
            TEST_CHECK(top.x == camera.x && top.y == camera.y && top.z == sea);
            const double height = terrain::unreal::heightAbovePlanet(camera, top, kRadius);
            TEST_CHECK(std::abs(height - altitude) < 1e-6);
        }
    }
    // The origin rebasing moves the world by an offset: the sea level moves with it.
    const UnrealPoint offset{-1.0e7, 2.5e6, -3.0e5};
    const UnrealFrame moved = terrain::unreal::shifted(frame, offset);
    TEST_CHECK(std::abs(terrain::unreal::seaLevelZ(moved) - (sea + offset.z)) < 1e-6);
}

// The sea of the horizon, lowered by the curvature of its material (d^2 / 2R), lies on the
// ground of the planet under the camera, within the gap between the parabola and the sphere
// (d^4 / 8R^3, 10.5 m at 520 km for a drop of 14 km), from the beach and from a summit.
void testCurvedSeaOnThePlanet() {
    const UnrealFrame frame = gaspFrame();
    const double sea = terrain::unreal::seaLevelZ(frame);
    for (const double altitude : {2.0, 4000.0}) {
        const UnrealPoint camera{1.0e9, -3.0e8, sea + altitude * kCm};
        const UnrealPoint top = terrain::unreal::planetTopUnder(camera, frame);
        for (const double d : {1.0e3, 5.0e4, 2.0e5, 5.2e5}) {
            const double drop = terrain::unreal::curvatureDropMeters(d, kRadius);
            const UnrealPoint point{camera.x + 0.6 * d * kCm, camera.y - 0.8 * d * kCm,
                                    sea - drop * kCm};
            const double gap = std::pow(d, 4.0) / (8.0 * std::pow(kRadius, 3.0));
            const double height = terrain::unreal::heightAbovePlanet(point, top, kRadius);
            TEST_CHECK(std::abs(height) <= gap * 1.01 + 1e-6);
            TEST_CHECK(std::abs(height) < 1e-3 * drop + 1e-6);
        }
    }
}

// The aerial perspective reaches the farthest point of the coarsest window.
void testAerialPerspectiveDepth() {
    terrain::HorizonParams params;
    params.levels = 10;
    const double depth = terrain::unreal::aerialPerspectiveDepthKm(params);
    TEST_CHECK(depth * 1000.0 >= terrain::unreal::levelReachMeters(params, 9));
    TEST_CHECK(depth - 1.0 < terrain::unreal::levelReachMeters(params, 9) / 1000.0);
    TEST_CHECK(depth > 700.0 && depth < 800.0 && depth == std::floor(depth));
    params.levels = 1;
    TEST_CHECK(terrain::unreal::aerialPerspectiveDepthKm(params) == 2.0);
}

double wavePhase(double x, double origin) {
    const double period = terrain::unreal::kSeaWavePeriodMeters * kCm;
    const double r = std::fmod(x - origin, period);
    return r < 0.0 ? r + period : r;
}

// The waves stay on the same place of the core: across a world turn (the player moved back by
// a whole world, the frame unchanged) and across the origin rebasing (the frame shifted).
void testWaveOrigin() {
    const double period = terrain::unreal::kSeaWavePeriodMeters * kCm;
    TEST_CHECK(std::fmod(terrain::kWorldAlignmentMeters, terrain::unreal::kSeaWavePeriodMeters) ==
               0.0);
    const terrain::WorldSize world;
    const UnrealFrame frame = gaspFrame();
    const terrain::unreal::WaveOrigin origin = terrain::unreal::seaWaveOrigin(frame);
    TEST_CHECK(origin.x >= 0.0 && origin.x < period && origin.y >= 0.0 && origin.y < period);
    // The core origin lies 22,960 m west and 4,060 m north of the Unreal origin.
    TEST_CHECK(origin.x == period - std::fmod(2296000.0, period));
    TEST_CHECK(origin.y == 406000.0);
    for (const double x : {0.0, 123457.0, -4.5e9, 4.4999e9}) {
        TEST_CHECK(wavePhase(x, origin.x) == wavePhase(x - world.x * kCm, origin.x));
        TEST_CHECK(wavePhase(x, origin.y) == wavePhase(x + world.z * kCm, origin.y));
    }
    const UnrealPoint offset{123456.0, -98765.0, 500.0};
    const terrain::unreal::WaveOrigin moved =
        terrain::unreal::seaWaveOrigin(terrain::unreal::shifted(frame, offset));
    for (const double x : {0.0, 777777.0, -3.0e9}) {
        const double before = wavePhase(x, origin.x);
        const double after = wavePhase(x + offset.x, moved.x);
        TEST_CHECK(std::abs(before - after) < 1e-3 ||
                   std::abs(std::abs(before - after) - period) < 1e-3);
        const double beforeY = wavePhase(x, origin.y);
        const double afterY = wavePhase(x + offset.y, moved.y);
        TEST_CHECK(std::abs(beforeY - afterY) < 1e-3 ||
                   std::abs(std::abs(beforeY - afterY) - period) < 1e-3);
    }
}

} // namespace

int main() {
    testFixedPlanetGoesBlack();
    testPlanetUnderCamera();
    testCurvedSeaOnThePlanet();
    testAerialPerspectiveDepth();
    testWaveOrigin();
    return test::result();
}

#pragma once

#include "unreal_frame.hpp"

#include "terrain/streaming/horizon_levels.hpp"

namespace terrain::unreal {

// The sky over the far horizon (phase 13).
//
// The material of the horizon curves the planet around the camera: the ground at a horizontal
// distance d from it lies d^2 / 2R below the plane of the sea level under it
// (curvatureDropMeters). The SkyAtmosphere of Unreal is a sphere of radius R standing still in
// the world. Left where the level places it, it holds a camera thousands of kilometers away high
// above its ground, in space: 5,000 km from its top, 1,230 km above its ground, far above its
// 60 to 100 km of air, and the sky is black (heightAbovePlanet). So its planet must stand under
// the camera, its top at the sea level right below it: the same sphere as the curvature of the
// horizon, whose sea then lies on its ground and whose horizon meets the horizon of the sky. The
// sky, its aerial perspective and the light of the sun are then the same everywhere on the
// planet, at every altitude.

// Unreal Z of the sea level (kSeaLevel), centimeters.
[[nodiscard]] double seaLevelZ(const UnrealFrame& frame);

// Where the top of the planet of the SkyAtmosphere stands (Transform Mode: Planet Top at
// Component Transform): at the sea level, right under the camera.
[[nodiscard]] UnrealPoint planetTopUnder(const UnrealPoint& camera, const UnrealFrame& frame);

// Height (meters) of a camera above the ground of a planet of this radius (meters) whose top
// stands at planetTop: what the SkyAtmosphere takes as its altitude.
[[nodiscard]] double heightAbovePlanet(const UnrealPoint& camera, const UnrealPoint& planetTop,
                                       double radiusMeters);

// Depth the aerial perspective of the SkyAtmosphere must cover (its camera volume,
// r.SkyAtmosphere.AerialPerspectiveLUT.Depth, 96 km by default), km: the farthest point of the
// coarsest window, rounded up to the kilometer. Beyond its depth the volume keeps its last
// slice: the far mountains would stay as clear as at 96 km.
[[nodiscard]] double aerialPerspectiveDepthKm(const HorizonParams& params);

// The waves of the sea of the horizon repeat every kSeaWavePeriodMeters along X and Y (the
// material uses whole wave numbers per period). The period divides every side of a world
// (kWorldAlignmentMeters): moving the player back by whole worlds leaves them in place.
inline constexpr double kSeaWavePeriodMeters = 4096.0;

// Unreal X and Y of the origin of the core modulo kSeaWavePeriodMeters, centimeters in
// [0, period): the material measures the waves from there, so that they stay at the same place
// of the core when the origin rebasing moves the frame.
struct WaveOrigin {
    double x = 0.0;
    double y = 0.0;

    friend bool operator==(const WaveOrigin&, const WaveOrigin&) = default;
};

[[nodiscard]] WaveOrigin seaWaveOrigin(const UnrealFrame& frame);

} // namespace terrain::unreal

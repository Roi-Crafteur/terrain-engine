#pragma once

namespace terrain::unreal {

// The shadows of the GPU horizon under a low sun (agent 40, FTerrainHorizonDrawing): its levels 0
// to 3 (up to 8 km) cast shadows while the sun is low, none otherwise. Under the orchestrator's
// measures, these four levels are the only ones whose shadows show (the long shadow of a massif
// of dunes over the far plain at 7 degrees), the levels 0 and 1 alone show nothing, and their cost
// is paid for nothing under a high sun. tests/horizon_low_sun_study_test.cpp chose the threshold.

inline constexpr int kLowSunShadowLevels = 4;
// Off again only that much above the threshold: a sun wavering around it never makes them blink.
inline constexpr double kLowSunHysteresisDegrees = 1.0;

// The elevation of the sun above the horizon, degrees, from the direction its light travels in
// (Unreal: z up; the light goes down while the sun is up), as Terrain.Sun sets it: the rotation
// (-elevation, heading + 90, 0) gives back the elevation. 0 for a null direction.
[[nodiscard]] double sunElevationDegrees(double dirX, double dirY, double dirZ);

// The levels that cast shadows under a sun that high: kLowSunShadowLevels once it is lower than
// belowDegrees, 0 once it is higher than belowDegrees + kLowSunHysteresisDegrees, current in
// between (current 0 or kLowSunShadowLevels); always 0 when belowDegrees is 0 or less.
[[nodiscard]] int lowSunShadowLevels(double sunElevation, double belowDegrees, int current);

} // namespace terrain::unreal

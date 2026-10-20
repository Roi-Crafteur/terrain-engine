#pragma once

namespace viewer {

// Tint of the terrain by altitude in the streamed world: green at sea level, gray from
// kStreamedHighAltitude.
inline constexpr float kStreamedLowAltitude = 0.0f;
inline constexpr float kStreamedHighAltitude = 2500.0f;

} // namespace viewer

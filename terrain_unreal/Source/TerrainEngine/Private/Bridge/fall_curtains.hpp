#pragma once

#include "tile_water_mesh.hpp"

#include "terrain/streaming/stream_manager.hpp"

namespace terrain::unreal {

// The share of the width of the channel at the lip that the curtain of a fall covers, how far in
// front of the lip it stands (meters, downstream), how far below the water of its pool it reaches
// (meters, so that no gap shows at the foot), and the radius of the foam at the foot as a share of
// the width (at least kFoamLeastRadius meters).
inline constexpr double kCurtainWidthShare = 0.9;
inline constexpr double kCurtainAhead = 0.5;
inline constexpr double kCurtainBelow = 0.5;
inline constexpr double kFoamRadiusShare = 0.75;
inline constexpr double kFoamLeastRadius = 3.0;

// The water of the falls whose lip lies in a tile (TileData::falls), in the frame of its ground
// (UnrealWaterMesh: centimeters from the origin of the tile). Only from the data of each fall —
// its lip, its foot, the water above and below and its width — never from the shape of its rock:
// - a fall steeper on average than kWaterCutSlope (whose face the surface of the tile cuts) gets a
//   curtain: a band kCurtainWidthShare of its width across, from the water at its lip down to
//   kCurtainBelow under the water at its foot, kCurtainAhead in front of the line from lip to foot
//   and curved as the water leaves the lip (it reaches a share sqrt(t) of the way to the foot after
//   falling a share t of the drop: free fall from a lip, outside the face from lip to foot), its
//   front face downstream; uv0 in meters across and down the fall;
// - at its foot, a disc of foam on the water below, kFoamRadiusShare of the width in radius.
// Every fall gives the same vertices, whatever the tile around it. Deterministic.
[[nodiscard]] UnrealWaterMesh buildFallCurtains(const TileData& data);

} // namespace terrain::unreal

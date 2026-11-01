#pragma once

#include "tile_water_mesh.hpp"

#include "terrain/streaming/stream_manager.hpp"

namespace terrain::unreal {

// The share of the width of the channel at the lip that the curtain of a fall covers, how far in
// front of the lip it stands (meters, downstream), how far below the water of its pool it reaches
// (meters, so that no gap shows at the foot), the radius of the foam at the foot as a share of
// the width (at least kFoamLeastRadius meters) and how far downstream of the foot of the curtain
// its center lies (a share of its radius: the foam spreads over the pool, not under the face),
// and the mean slope (drop over run) from which the curtain leaves the rock in a curve.
inline constexpr double kCurtainWidthShare = 0.9;
inline constexpr double kCurtainAhead = 0.5;
inline constexpr double kCurtainBelow = 0.5;
inline constexpr double kFoamRadiusShare = 0.75;
inline constexpr double kFoamLeastRadius = 3.0;
inline constexpr double kFoamAheadShare = 0.35;
inline constexpr double kCurtainCurveSlope = 1.5;
// Along the edge of a lip, a curtain goes on kCurtainSides meters beyond its two ends.
inline constexpr double kCurtainSides = 1.0;

// The water of the falls whose lip lies in a tile (TileData::falls, here), in the frame of its
// ground (UnrealWaterMesh: centimeters from the origin of the tile). Only from the data of each
// fall — its lip and the edge of it, its foot, the water above and below, its width and its pool
// — never from the shape of its rock:
// - every fall gets a curtain (the surface of the tile is cut where its water is steeper than
//   kWaterCutSlope, the slope of the steps of a fall in the core). Along the edge of its lip when
//   the core gives it (TileFall::edgeX: receded, bowed, turned): from each point of the edge down
//   to where its water meets the pool (footEdgeX), kCurtainBelow under the water there. Without
//   it, a band kCurtainWidthShare of its width across, from the lip down to its foot. Always
//   kCurtainAhead in front; steeper on average than kCurtainCurveSlope, it is curved as the water
//   leaves the lip (it reaches a share sqrt(t) of the way to the foot after falling a share t of
//   the drop: free fall from a lip, outside the face from lip to foot); less steep, it is
//   straight from lip to foot (a chute). Its front face downstream; uv0 in meters across and
//   down the fall, uv2.v its width;
// - at its foot (that of the middle of its edge), a disc of foam on the water below,
//   kFoamRadiusShare of the width of the water falling (its edge, or its channel) in radius, never
//   past the end of its pool (TileFall::pool), centered kFoamAheadShare of its radius downstream
//   (uv0 meters from its center, uv2.v its radius).
// Every fall gives the same vertices, whatever the tile around it. Deterministic.
[[nodiscard]] UnrealWaterMesh buildFallCurtains(const TileData& data);

} // namespace terrain::unreal

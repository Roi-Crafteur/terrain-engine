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

// The water of the falls of a tile (TileData::falls), in the frame of its ground
// (UnrealWaterMesh: centimeters from the origin of the tile). Only from the data of each fall —
// its lip and the edge of it, its foot, the water above and below, its width and its pool — and
// the water of the tile, never from the shape of its rock:
// - every fall whose lip lies in the tile (here) gets a curtain (the surface of the tile is cut
//   where its water is steeper than kWaterCutSlope, the slope of the steps of a fall in the core).
//   Along the edge of its lip when the core gives it (TileFall::edgeX: receded, bowed, turned),
//   in one piece where the water falls from it (TileFall::wetFrom to wetTo: from where it arrives
//   at the edge to where it stops, a rock in the middle never splitting it; without them, within
//   its channel), never over the dry walls of its slot at its ends: from each point of the edge
//   down to where its water meets the pool (footEdgeX), kCurtainBelow under the water there.
//   Without it, a band kCurtainWidthShare of its width across, from the lip down to its foot.
//   Always kCurtainAhead in front; steeper on average than kCurtainCurveSlope, it is curved as
//   the water leaves the lip (it reaches a share sqrt(t) of the way to the foot after falling a
//   share t of the drop: free fall from a lip, outside the face from lip to foot); less steep, it
//   is straight from lip to foot (a chute). Its front face downstream; uv0 in meters across and
//   down the fall, uv2.v its width;
// - every fall here or near draws its foam on the tile: centered kFoamAheadShare of its radius
//   downstream of its foot (that of the middle of its edge), kFoamRadiusShare of the width of its
//   channel in radius (kFoamLeastRadius at least), never past the end of its pool
//   (TileFall::pool), its ragged rim two meters short of the edge of the lip of the fall its pool
//   runs to (nearer on a side when that lip lies in a slant or bowed), on the cells of the tile
//   whose four corners stand in the water of its pool (drawnWaterLevels near the water below the
//   fall), a few centimeters over it: never over a ramp, a bank, a lip or another pool, the same
//   in the tiles beside (uv0 meters from its center, uv1.v 0 on the border of its pool and 1
//   within, uv2.v its radius: the material fades it out to a ragged rim and toward the border,
//   never a step of the cells there).
// Every fall gives the same vertices, whatever the tile around it. Deterministic.
[[nodiscard]] UnrealWaterMesh buildFallCurtains(const TileData& data);

} // namespace terrain::unreal

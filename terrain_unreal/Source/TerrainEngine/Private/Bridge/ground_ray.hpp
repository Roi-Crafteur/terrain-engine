#pragma once

#include "tile_streamer.hpp"
#include "unreal_frame.hpp"

#include "terrain/chunk_coord.hpp"
#include "terrain/streaming/lod.hpp"

#include <functional>
#include <optional>

namespace terrain::unreal {

// The point aimed at by the tools of the developers (agent 36: the screen of F3, Terrain.Info)
// beyond the tiles that have a collision (LOD 0, about 64 m): the ray of the camera marched
// against the ground of the core.

// A direction of Unreal (X, Y, Z: a forward vector) as a direction of the core: the axes
// swapped as unreal_frame.hpp swaps them, not scaled.
[[nodiscard]] CorePoint coreDirection(double unrealX, double unrealY, double unrealZ);

struct GroundHit {
    CorePoint point;       // on the ground, meters
    double distance = 0.0; // along the ray from its origin, meters
    int samples = 0;       // heights read
};

// The first point where the ray from origin along direction (a unit vector of the core) goes
// down to the ground of height(x, z), within maxDistance meters, or nullopt (the sky, or the
// budget of maxSamples heights spent). The steps grow with the clearance over the ground (half
// of it: no slope under 63 degrees is stepped over) and with the distance, then the crossing is
// halved down to a centimeter. An origin under the ground is its own hit. Display only: not a
// part of the world.
[[nodiscard]] std::optional<GroundHit> marchGround(
    const CorePoint& origin, const CorePoint& direction, double maxDistance,
    const std::function<double(double x, double z)>& height, int maxSamples = 2000);

// The shown tile holding a chunk (any image of it), the finest LOD shown there: the tile of a
// finer LOD replaces the coarser one only once all its siblings are shown (TileStreamer). nullopt
// where no tile is shown.
[[nodiscard]] std::optional<TileKey> shownTileAt(const TileStreamer& streamer, ChunkCoord chunk);

} // namespace terrain::unreal

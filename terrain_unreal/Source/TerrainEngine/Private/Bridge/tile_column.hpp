#pragma once

// The ground a tile mesh puts on a column (agent 41, the second test of #78 in Unreal): the
// highest point of its triangles on the vertical line through a point, as the ground is seen from
// above there. The same for the data of the core, for what the streamer hands to Unreal, and for
// what a mesh component of Unreal holds (Terrain.EditTiles): where they differ, the tile shown is
// not the tile of the core.

#include "tile_streamer.hpp"
#include "unreal_frame.hpp"

#include "terrain/streaming/stream_manager.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace terrain::unreal {

// The highest point of the triangles (three indices each, into vertices) on the vertical line
// through (x, z), meters in the core frame; none where no triangle crosses it (a hole, or the
// line outside the mesh). A point on an edge counts for the triangles on both sides of it.
[[nodiscard]] std::optional<double> surfaceTop(std::span<const CorePoint> vertices,
                                               std::span<const std::uint32_t> triangles,
                                               double x, double z);
[[nodiscard]] std::optional<double> surfaceTop(std::span<const CorePoint> vertices,
                                               std::span<const std::int32_t> triangles,
                                               double x, double z);

// Every surface the triangles put on the vertical line through (x, z), highest first, those
// within a millimeter of each other once (a point on an edge): the ground, then under it the
// ceiling and the floor of a tunnel, a cave or an overhang.
[[nodiscard]] std::vector<double> surfaceHeights(std::span<const CorePoint> vertices,
                                                 std::span<const std::uint32_t> triangles,
                                                 double x, double z);

// The vertices of the mesh of a tile in the core frame: those of its data (at the canonical
// image), and those of an upload placed in Unreal (at the image it is shown at).
[[nodiscard]] std::vector<CorePoint> coreVertices(const TileData& data);
[[nodiscard]] std::vector<CorePoint> coreVertices(const UnrealFrame& frame,
                                                  const TileUpload& upload);

} // namespace terrain::unreal

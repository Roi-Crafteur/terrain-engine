#pragma once

// The water of the columns of a streamed tile and its falls (TileData::waterColumns,
// TileData::falls), internal to terrain_core: for terrain_tiles.cpp.

#include "tile_lattice.hpp"

#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/stream_manager.hpp"

namespace terrain::tile {

// Fills data.waterColumns and data.falls when field has a hydrology and a column of the tile
// holds a river or a lake (leaves them empty otherwise): the kind and level of each column, bit
// for bit those of columns; on the river columns, the direction downstream of the segment of the
// course nearest to the column among the reaches within kFlowReach meters of the tile; the falls
// of those reaches whose lip lies in the tile. Pure arithmetic on the columns and on the courses
// of the hydrology (computed on demand, the same bits in any order): deterministic, thread-safe.
void addRiverWater(TileData& data, const Heightfield& field, const TileLattice& lattice,
                   const TileColumns& columns);

// How far beyond a tile the courses of its rivers are searched: a column of a channel lies within
// half its width of its axis.
inline constexpr double kFlowReach = 1024.0;

} // namespace terrain::tile

#pragma once

// The surface blend of a streamed tile (TileData::surfaceBlend), internal to terrain_core: for
// terrain_tiles.cpp.

#include "tile_lattice.hpp"

#include "terrain/generation/biome_blend.hpp"
#include "terrain/generation/ecology.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/stream_manager.hpp"

#include <vector>

namespace terrain::tile {

// The texels of a tile, one every stride columns (stride divides kChunkCells), row by row from
// its lowest corner. The site of a texel is heightfieldSite of its column and of its 4 neighbor
// columns one cell away (outside the tile, the heights of the field there), its climate and its
// blend those of the ecology at its position: every texel is a function of its world position
// and of the cell of the lattice only, never of the tile.
[[nodiscard]] std::vector<BlendTexel> blendTexels(const Heightfield& field, const Ecology& ecology,
                                                  const TileLattice& lattice,
                                                  const TileColumns& columns,
                                                  int stride = kTileBlendStride);

} // namespace terrain::tile

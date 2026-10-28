#pragma once

// The water level of the vertices of a streamed tile (TileData::waterLevels), internal to
// terrain_core: for terrain_tiles.cpp.

#include "tile_lattice.hpp"

#include "terrain/generation/cave_materials.hpp"
#include "terrain/meshing/mesh.hpp"

#include <span>
#include <vector>

namespace terrain::tile {

// The water over each vertex of mesh (positions relative to the lowest corner of the lattice, in
// meters): the levels of the wet columns of its cell, weighted bilinearly at its position, or
// kNoWaterLevel where none of the columns with a weight is wet. A vertex on a column (every
// vertex of the ground but those on the sides of its steps) takes exactly the level of that
// column, HeightSample::waterLevel bit for bit; a vertex on an edge between two columns their
// interpolated level, or the level of the wet one. With caves (materials, one per vertex, or
// empty), only the vertices of the outer surface take the water of their column, not the walls
// of the caves under it. A pure function of the columns and of the positions.
[[nodiscard]] std::vector<float> vertexWaterLevels(const TileColumns& columns,
                                                   const TileLattice& lattice, const Mesh& mesh,
                                                   std::span<const CaveMaterial> materials);

} // namespace terrain::tile

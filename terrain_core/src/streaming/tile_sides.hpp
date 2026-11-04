#pragma once

// The light of the sides of the tiles of the ground (agent 34), internal to terrain_tiles.cpp:
// Marching Cubes differences the density one-sided on the faces of its grid, so the normal of a
// vertex on a side of a tile differed from the one the neighboring tile of the same LOD gives the
// same vertex (up to a degree on the dunes): at a low sun, a straight line where they meet.

#include "terrain/meshing/mesh.hpp"
#include "tile_lattice.hpp"

namespace terrain::tile {

// Sets the normals of the vertices of a level of a tile of the ground alone (its mesh in cells,
// as Marching Cubes gives it) that lie on its sides, but on those of the seams (sides is the
// TileSeam bits of the faces stitched to a coarser neighbor): the centered differences of its
// ground across the side with the ring of the columns beyond it (TileColumns::ringHeights), as
// Marching Cubes takes them inside the grid. spacing: the meters of a cell.
void lightSides(Mesh& level, const TileColumns& columns, unsigned seams, int spacing);

} // namespace terrain::tile

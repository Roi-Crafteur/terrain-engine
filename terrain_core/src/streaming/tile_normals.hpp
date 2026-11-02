#pragma once

// The normals of the meshes of the streamed tiles against their triangles (phase 12A), internal
// to terrain_tiles.cpp: on a feature as thin as a cell (a rib of rock, a steep riser met by a
// side of a tile), the differences of the density over two cells may point a normal against the
// surface its triangles draw, and a client lights them as if seen from behind.

#include "terrain/meshing/mesh.hpp"

namespace terrain::tile {

// Turns the normal of each vertex of a mesh (in its level, as Marching Cubes gives it) whose
// cosine with the normal of its triangles (summed by their areas) is below kAgree just far
// enough toward theirs, in the plane of both; then turns the normals of the vertices of the
// triangles still lit against their winding (a fold of the surface) in front of them, a little
// more at each round; a vertex where triangles meet back to back takes the normal that faces each
// of them a little. Elsewhere the normals keep their bits.
void agreeWithTriangles(Mesh& mesh);

} // namespace terrain::tile

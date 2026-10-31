#pragma once

#include "altitude_color.hpp"
#include "dev_commands.hpp"
#include "tile_mesh_conversion.hpp"
#include "unreal_frame.hpp"

#include "terrain/generation/heightfield.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace terrain::unreal {

// The colors of the vertices of the tiles in the view modes of the tools of the developers
// (agent 36, Terrain.View lod, roche, normales, pente), shown by the vertex color material of
// Unreal: pure functions of the meshes Unreal holds, nothing of the world changed.

// One hue per LOD: red, yellow, green, blue.
[[nodiscard]] VertexColor lodColor(int lod);

// The slope of a unit normal (Unreal, Z up): green under 15 degrees, yellow-green to 30, yellow
// to 45, orange to 60, red to 80, magenta to vertical, blue under an overhang (facing down).
[[nodiscard]] VertexColor slopeColor(const UnrealVector3f& normal);

// A unit normal as a color: (n + 1) / 2 on each axis of Unreal (X east red, Y south green, Z up
// blue), as the world normals of Unreal show them.
[[nodiscard]] VertexColor normalColor(const UnrealVector3f& normal);

// The heights of the lattice of a tile of this LOD whose lowest corner lies at (x, z), core
// meters: kGridSamples x kGridSamples heights of field, (i, k) at index k * kGridSamples + i.
[[nodiscard]] std::vector<float> tileLatticeHeights(const Heightfield& field, int lod, double x,
                                                    double z);

// Which vertices of the mesh of a tile (centimeters from origin, the lowest corner of the tile
// in the core) lie off the heightmap: more than tolerance meters (half a cell, 0.5 m at least)
// above or below the heights of its lattice, bilinear: the ground there is not the heightmap
// (the rock of the walls in three dimensions, a cave, an edit). The vertices on the sides of
// the tile, which the seams with coarser tiles move, are never off it. 1: off, 0: on.
[[nodiscard]] std::vector<std::uint8_t> offHeightmap(const UnrealMesh& mesh, int lod,
                                                     const CorePoint& origin,
                                                     std::span<const float> lattice);

// The colors of the vertices of a mesh in a view mode: by LOD, by slope, by normal, or gray with
// the vertices off the heightmap in orange (off, one per vertex); the colors of the mesh in the
// other modes.
[[nodiscard]] std::vector<VertexColor> debugColors(ViewMode mode, const UnrealMesh& mesh, int lod,
                                                   std::span<const std::uint8_t> off);

// Whether a view mode colors the vertices of the tiles.
[[nodiscard]] bool colorsVertices(ViewMode mode);

} // namespace terrain::unreal

#pragma once

#include "terrain/meshing/mesh.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace terrain {

// Checks of the mesh of a tile of the streamed world (phase 12A), as a client receives it: its
// openings and its triangles turned the wrong way, marked for the views (mesh_view.hpp) and
// counted for the tests.
//
// The vertices are welded by their exact positions (the levels of a tile share the vertices of
// the plane between them bit for bit). An edge of a single triangle is an opening unless it lies
// on the outline of the tile, the planes x = 0, x = side, z = 0 or z = side (the seams, which the
// tests of the seams check); an edge of more than two triangles is not manifold. A triangle is
// inverted when the normals of its vertices, summed, point against its winding (counter-clockwise
// seen from the air: the side a client lights and does not cull). Degenerate triangles (two
// vertices welded together, or no area) cover nothing and are counted apart.

inline constexpr std::uint8_t kMarkSound = 0;
inline constexpr std::uint8_t kMarkOpen = 1;     // has an edge of an opening
inline constexpr std::uint8_t kMarkInverted = 2; // its normals against its winding

struct MeshCheck {
    std::size_t triangles = 0;
    std::size_t degenerate = 0;
    std::size_t openEdges = 0;
    std::size_t nonManifoldEdges = 0;
    std::size_t inverted = 0;
    std::vector<std::uint8_t> marks; // per triangle
};

// The checks of a tile mesh whose positions lie in [0, side] along x and z.
[[nodiscard]] MeshCheck checkTileMesh(const Mesh& mesh, float side);

} // namespace terrain

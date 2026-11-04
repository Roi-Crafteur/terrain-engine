#pragma once

#include <array>

namespace terrain::unreal {

// How the GPU horizon cuts each cell of its grids into triangles (agent 40). The material of the
// GPU horizon (Scripts/create_horizon_gpu_material.py) interpolates the attributes of its vertices
// linearly on these triangles, the heights and the normals of the grid among them: these are its
// reference, as horizon_morph.hpp is for the morph.
//
// - Fixed: every cell along the diagonal from (i + 1, k) to (i, k + 1) (horizon_mesh.cpp, the
//   morph targets of horizon_morph.hpp), the grids before agent 40. A wall, a ridge or a coast line
//   oblique to the grid along the other diagonal is cut into a tooth per cell.
// - Contour: the diagonal whose two ends have the nearer heights, the one that runs along the
//   contour lines through the cell; a tie keeps the fixed one. (The grids of agent 40 hold a vertex
//   at the center of each cell: at the mean of the two ends of the chosen diagonal, the four
//   triangles around it are those of that diagonal.)
enum class CellSplit { Fixed, Contour };

// Whether a cell is cut along its main diagonal, from (i, k) to (i + 1, k + 1), its corners h00 at
// (i, k), h10 at (i + 1, k), h01 at (i, k + 1), h11 at (i + 1, k + 1): never with Fixed; with
// Contour when |h00 - h11| < |h10 - h01|, in float as the shader compares them.
[[nodiscard]] bool mainDiagonal(CellSplit split, float h00, float h10, float h01, float h11);

// The weights of the corners (00, 10, 01, 11) at (u, v) of a cell, u and v in [0, 1] from its
// corner 00 along i and k: those of the triangle holding (u, v), linear, the corner across the
// diagonal 0. They sum to 1.
[[nodiscard]] std::array<double, 4> cellWeights(bool main, double u, double v);

} // namespace terrain::unreal

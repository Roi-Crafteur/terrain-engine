#pragma once

#include "horizon_textures.hpp"

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
//   contour lines through the cell; a tie keeps the fixed one (withCellCenters, centerOnMain).
enum class CellSplit { Fixed, Contour };

// Whether a cell is cut along its main diagonal, from (i, k) to (i + 1, k + 1), its corners h00 at
// (i, k), h10 at (i + 1, k), h01 at (i, k + 1), h11 at (i + 1, k + 1): never with Fixed; with
// Contour when |h00 - h11| < |h10 - h01|, in float as the shader compares them.
[[nodiscard]] bool mainDiagonal(CellSplit split, float h00, float h10, float h01, float h11);

// The grids of the GPU horizon with a vertex for each cell (GridVertexKind::Center): the two
// triangles {00, 01, 10}, {10, 01, 11} of every cell of samples (bodyGrid, trimGrid) become the
// four {00, 01, C}, {01, 11, C}, {11, 10, C}, {10, 00, C} around a vertex C added after the others,
// standing on the corner (i + 1, k) of its cell. The vertex shader of the horizon
// (Scripts/horizon_vertex_hlsl.py) leaves it there, where the four triangles are exactly the two
// of the fixed diagonal and two of no area (AdaptiveSplit 0: the grids before agent 40, to the
// bit), or moves it onto the corner (i, k), where they are the two of the main diagonal and two of
// no area. The other triangles (skirts, sea square) are kept as they are.
[[nodiscard]] GridMesh withCellCenters(const GridMesh& grid);

// Where the vertex shader puts the center vertex of a cell (centerOnMain: on its corner (i, k),
// the main diagonal; otherwise on (i + 1, k), the fixed one).
struct CenterInput {
    bool adaptive = true;          // the switch AdaptiveSplit of the material
    std::array<float, 4> own{};    // the heights of the corners 00, 10, 01, 11 of the cell
    bool hasCoarser = false;       // a coarser level the morph goes toward
    std::array<float, 4> coarse{}; // the corners of the cell of the coarser level holding it
    int quarterI = 0;              // where the cell lies in that cell: i % 2 and k % 2
    int quarterK = 0;
    double morphAtCenter = 0.0;    // the morph toward the coarser level at the center of the cell
};
// Without the switch, never. Otherwise the contour of its own corners (mainDiagonal), except past
// the middle of the morph toward the coarser level, where a cell along the diagonal the coarser
// cell is cut along takes that diagonal (at the end of the morph its corners lie on the coarser
// triangles, so that the surface is the coarser one); a cell off it lies within one of the coarser
// triangles, where both its diagonals give the same surface, and keeps its own.
[[nodiscard]] bool centerOnMain(const CenterInput& in);

// The same cut for the odd samples of both axes at the middle of a coarser cell, whose morph goes
// toward the mean of the two ends of the diagonal the coarser cell is cut along: the main one when
// true (the fixed one, (i + 1, k)-(i, k + 1) of the coarser cell, without the switch).
[[nodiscard]] bool coarserOnMain(bool adaptive, const std::array<float, 4>& coarse);

// The weights of the corners (00, 10, 01, 11) at (u, v) of a cell, u and v in [0, 1] from its
// corner 00 along i and k: those of the triangle holding (u, v), linear, the corner across the
// diagonal 0. They sum to 1.
[[nodiscard]] std::array<double, 4> cellWeights(bool main, double u, double v);

} // namespace terrain::unreal

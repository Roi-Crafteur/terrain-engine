#pragma once

#include "terrain/io/png.hpp"
#include "terrain/io/relief_view.hpp"
#include "terrain/meshing/mesh.hpp"

#include <span>

namespace terrain {

// Views of meshes, as a client draws the tiles of the streamed world (phase 12A): a control image
// of the rock in three dimensions (walls, overhangs, niches, arches), which the views of the
// relief (relief_view.hpp), cast over a height per column, cannot show. Not a renderer: the
// triangles are drawn with a depth buffer, lit by a low sun on the left of the view through the
// normals of their vertices, in the hypsometric colors of relief_png turned to bare rock on steep
// faces (as the material of the tiles in Unreal), hazed with the distance. The rows of the image
// are drawn in fixed bands on the threads, each band taking every triangle in order: the image
// never depends on the number of threads.

// A mesh to draw: its positions are relative to an origin, meters (a tile of the streamed world:
// TileData::mesh at its origin).
struct ViewMesh {
    const Mesh* mesh = nullptr;
    double originX = 0.0;
    double originY = 0.0;
    double originZ = 0.0;
    // The share of bare rock of each vertex, in [0, 1]; empty: from the slope of its normal.
    std::span<const float> rock;
    bool water = false; // a water surface, drawn in the color of the water
};

// The meshes seen from the eye of params, over background: the view of what lies beyond them, of
// the size of the image and RGB (a renderReliefView of the same params, its first depth beyond
// the meshes), which shows wherever no triangle is drawn. The curvature of the planet lowers the
// meshes as it lowers the relief of the views. Throws std::invalid_argument for invalid params
// (as renderReliefView), a background of another size or not RGB, or a mesh with an index out of
// range or rock shares of another count than its vertices.
[[nodiscard]] Image renderMeshView(std::span<const ViewMesh> meshes, const Image& background,
                                   const ViewParams& params, int threads);

// A view from straight above, without perspective: +X to the right, +Z down, pixel (i, j) the
// highest surface of the meshes at (centerX + (i + 0.5 - width / 2) metersPerPixel, centerZ +
// (j + 0.5 - height / 2) metersPerPixel), lit by a low sun from sunAzimuth (turns clockwise from
// the north, -Z) at an elevation of sine sunSine; black where no mesh lies.
struct TopViewParams {
    double centerX = 0.0;
    double centerZ = 0.0;
    double metersPerPixel = 0.25;
    int width = 1200;
    int height = 1200;
    double sunAzimuth = 0.875; // from the north-west
    double sunSine = 0.35;     // about 20 degrees
};

// Throws std::invalid_argument for an image smaller than 2 x 2 pixels, a scale that is not finite
// and above 0, a sun at or below the horizon, or a mesh as renderMeshView.
[[nodiscard]] Image renderTopView(std::span<const ViewMesh> meshes, const TopViewParams& params,
                                  int threads);

} // namespace terrain

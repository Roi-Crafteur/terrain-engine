#pragma once

#include "colored_mesh.hpp"
#include "math.hpp"
#include "terrain/meshing/mesh.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace viewer {

// Eroded region of kErodedChunks x kErodedChunks chunks of 16 x 16 cells of
// kErodedMetersPerCell meters, around a summit of the heightfield (about 12.8 km wide).
inline constexpr int kErodedChunks = 16;
inline constexpr double kErodedMetersPerCell = 50.0;

// Geometry of the eroded region as a scale model: one unit is one grid cell
// (kErodedMetersPerCell meters) on every axis, so its shape is exact and the camera crosses
// it at the usual speed.
struct ErodedRegionView {
    terrain::Mesh terrain;             // Marching Cubes of density y - height, chunk by chunk
    std::vector<ColoredVertex> water;  // triangles: river ribbons, cascades and the sea
    float minHeight = 0.0f;            // model units
    float maxHeight = 0.0f;
    Vec3 viewpoint;                    // south of the region, above it
    std::size_t riverCells = 0;
    std::size_t cascades = 0;
};

// Erodes the region (terrain::ErodedRegion: erosion, then rivers and cascades) and builds
// its geometry. Deterministic for a given seed.
[[nodiscard]] ErodedRegionView buildErodedRegionView(std::uint64_t seed);

} // namespace viewer

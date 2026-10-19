#pragma once

#include "colored_mesh.hpp"
#include "math.hpp"
#include "terrain/meshing/mesh.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace viewer {

// Cliff region of kCliffGridSize x kCliffGridSize samples kCliffMetersPerPixel apart around
// the cliff site of the world (terrain::findCliffRegion), about 6.4 km wide.
inline constexpr int kCliffGridSize = 257;
inline constexpr double kCliffMetersPerPixel = 25.0;
// The density volume is meshed every kCliffMetersPerUnit meters, on every axis: one model
// unit is kCliffMetersPerUnit meters, so the vertical walls keep their shape.
inline constexpr double kCliffMetersPerUnit = 12.5;

// Geometry of the cliff region as a scale model.
struct CliffRegionView {
    terrain::Mesh terrain;            // Marching Cubes of the cliff density, chunk by chunk
    std::vector<ColoredVertex> water; // triangles: river ribbons, cascades and the sea
    float minHeight = 0.0f;           // model units
    float maxHeight = 0.0f;
    Vec3 viewpoint;                   // south of the region, above it
    std::size_t riverCells = 0;
    std::size_t cascades = 0;
    std::size_t cirques = 0;
};

// Builds terrain::CliffRegion (erosion, rivers, then cliffs, plateaus and gorges) and its
// geometry. Deterministic for a given seed.
[[nodiscard]] CliffRegionView buildCliffRegionView(std::uint64_t seed);

} // namespace viewer

#pragma once

#include "terrain/meshing/density_grid.hpp"
#include "terrain/meshing/mesh.hpp"

namespace terrain {

// Density of the extracted surface.
inline constexpr float kIsoLevel = 0.0f;

// Extracts the surface density == kIsoLevel with Marching Cubes (Paul Bourke's tables).
// Each crossed grid edge holds exactly one vertex shared by its cells; normals come from
// the density gradient. Deterministic: the same grid always gives the same mesh.
[[nodiscard]] Mesh marchingCubes(const DensityGrid& grid);

} // namespace terrain

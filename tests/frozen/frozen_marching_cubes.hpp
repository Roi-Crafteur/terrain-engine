#pragma once

// FROZEN REFERENCE, do not edit: marchingCubes as of commit 7cf4877, before the meshing
// performance work (frozen_marching_cubes.cpp). marching_cubes_frozen_test compares the live
// function with it bit for bit.

#include "terrain/meshing/density_grid.hpp"
#include "terrain/meshing/mesh.hpp"

namespace frozen {

[[nodiscard]] terrain::Mesh marchingCubes(const terrain::DensityGrid& grid);

} // namespace frozen

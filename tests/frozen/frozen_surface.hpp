#pragma once

// FROZEN REFERENCE, do not edit: surfaceFromHeightfield and sampleHeightGrid as of commit
// 052078b, before the noise performance work, reading the relief point by point
// (frozen_surface.cpp). height_access_test compares the live functions with them bit for bit.

#include "terrain/generation/height_grid.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/terrain_surface.hpp"

namespace frozen {

[[nodiscard]] terrain::TerrainSurface surfaceFromHeightfield(
    const terrain::Heightfield& field, const terrain::HeightmapRegion& region);
[[nodiscard]] terrain::HeightGrid sampleHeightGrid(const terrain::Heightfield& field,
                                                   const terrain::HeightmapRegion& region);

} // namespace frozen

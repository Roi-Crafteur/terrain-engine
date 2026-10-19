#pragma once

#include "colored_mesh.hpp"
#include "math.hpp"
#include "terrain/meshing/mesh.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace viewer {

// Square window of kSculptedWindowMeters of the cliff region (the region of the cliff mode,
// see cliff_region_view.hpp) with the most arches and overhangs, avoiding arches cut by its edge,
// meshed every kSculptedMetersPerUnit meters on every axis: one model unit is
// kSculptedMetersPerUnit meters, fine enough for the notches under the banks of the strata
// and the tubes of the arches.
inline constexpr double kSculptedWindowMeters = 640.0;
inline constexpr double kSculptedMetersPerUnit = 2.5;

// Geometry of the window as a scale model.
struct SculptedRegionView {
    terrain::Mesh terrain;            // Marching Cubes of the sculpted density, chunk by chunk
    std::vector<ColoredVertex> water; // triangles: river ribbons, cascades and the sea
    float minHeight = 0.0f;           // model units
    float maxHeight = 0.0f;
    Vec3 viewpoint;                   // south of the window, above it
    std::size_t arches = 0;           // in the whole region
    std::size_t windowArches = 0;     // in the window
    std::size_t overhangSamples = 0;  // samples of the window with rock above air
};

// Builds terrain::SculptedRegion (cliffs, then overhangs and arches) on the cliff region and
// the geometry of the window. Deterministic for a given seed.
[[nodiscard]] SculptedRegionView buildSculptedRegionView(std::uint64_t seed);

} // namespace viewer

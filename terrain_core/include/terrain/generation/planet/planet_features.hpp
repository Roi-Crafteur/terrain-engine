#pragma once

#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_land.hpp"
#include "terrain/generation/planet/planet_params.hpp"
#include "terrain/generation/planet/planet_rivers.hpp"

#include <cstdint>
#include <vector>

namespace terrain {

enum class RiverFeatureKind : std::uint8_t {
    Delta,     // a sea mouth with a gentle river and a shallow sea: a fan of distributaries
    Estuary,   // another large sea mouth: a drowned, widening river mouth
    Waterfall, // a step much steeper than the river above and below it (an escarpment)
    Gorge,     // a stretch of river walled in by high ground on both sides
};

// A place of the river graph for 11B, on the points [firstVertex, lastVertex] of a segment (the
// same point for a mouth or a waterfall, whose step goes on to the next point).
struct RiverFeature {
    RiverFeatureKind kind = RiverFeatureKind::Delta;
    std::int32_t segment = -1;
    std::int32_t firstVertex = 0;
    std::int32_t lastVertex = 0;
    double x = 0.0; // meters: the mouth, the middle of the step, the deepest point of the gorge
    double z = 0.0;
    float discharge = 0.0f; // m^3/s
    // Delta and estuary: the slope of the last reach; waterfall: its drop (m); gorge: its
    // greatest depth (m).
    float magnitude = 0.0f;
};

namespace planet {

// The deltas, estuaries, waterfalls and gorges of a river graph (PlanetFeatureParams), segment
// by segment in order. The slopes before and after a step follow the river across the
// confluences: upstream along the branch of greatest discharge, downstream into the segment
// joined.
[[nodiscard]] std::vector<RiverFeature> findRiverFeatures(const PlanetGrid& grid,
                                                          const std::vector<float>& height,
                                                          const LandIndex& land,
                                                          const RiverGraph& graph,
                                                          const PlanetFeatureParams& params);

} // namespace planet

} // namespace terrain

#pragma once

// Épinglage réservé aux tests : the relief of generator 2 (local continentalness, macro and plate
// layers, no planetary geography), for the tests whose places, regions and thresholds were
// chosen in that world (a cave district at a given point, a cliff site, a coast of a region).
// Never used outside tests/: the game, the tools and the viewer build HeightfieldParams{}.
// Each module so pinned also has a test of the default world that finds its own places by search.
#include "terrain/generation/cave_region.hpp"
#include "terrain/generation/cliff_region.hpp"
#include "terrain/generation/eroded_region.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/sculpted_region.hpp"

namespace test_relief {

[[nodiscard]] inline terrain::HeightfieldParams legacyRelief() {
    terrain::HeightfieldParams params;
    params.geography.enabled = false;
    return params;
}

// The parameters of a relief or of a region, the rest unchanged, on legacyRelief().
[[nodiscard]] inline terrain::HeightfieldParams onLegacyRelief(terrain::HeightfieldParams p) {
    p.geography = legacyRelief().geography;
    return p;
}

[[nodiscard]] inline terrain::ErodedRegionParams onLegacyRelief(terrain::ErodedRegionParams p) {
    p.heightfield = onLegacyRelief(p.heightfield);
    return p;
}

[[nodiscard]] inline terrain::CliffRegionParams onLegacyRelief(terrain::CliffRegionParams p) {
    p.eroded = onLegacyRelief(p.eroded);
    return p;
}

[[nodiscard]] inline terrain::SculptedRegionParams onLegacyRelief(terrain::SculptedRegionParams p) {
    p.cliffs = onLegacyRelief(p.cliffs);
    return p;
}

[[nodiscard]] inline terrain::CaveRegionParams onLegacyRelief(terrain::CaveRegionParams p) {
    p.eroded = onLegacyRelief(p.eroded);
    return p;
}

} // namespace test_relief

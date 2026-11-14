#pragma once

// Internal to terrain_core: where the volcanoes of a world stand (agent 45). Drawn from the seed of
// the world on the plates of its geography with the constants of the core (VolcanoParams): in the
// arcs behind the subductions of oceanic plates, along the continental rifts and where the ridges
// reach the land, at hotspots anywhere, kept apart; the active and the large among them drawn too.
// The relief (volcano_sites.cpp) and the settlement (settlement/geology.cpp) read the same list:
// no data file decides it. Deterministic, the same bits whatever the threads.

#include "geography_plates.hpp"
#include "terrain/generation/volcanoes.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>
#include <vector>

namespace terrain {

// A volcano where the plates put it: its center (canonical, meters), its origin, whether it is
// active and whether it is large.
struct PlacedVolcano {
    double x = 0.0;
    double z = 0.0;
    VolcanoOrigin origin = VolcanoOrigin::Arc;
    bool active = false;
    bool large = false;
};

// The share of the area of the reference planet (VolcanoParams::referenceAreaKm2) a world covers:
// the counts of params are scaled by it.
[[nodiscard]] double volcanoScale(const WorldSize& world, const VolcanoParams& params);

// The volcanoes of the world of plates, whose Heightfield has the seed worldSeed, by the counts
// and rules of params scaled by scale, on up to threads threads. params.enabled is not read: the
// settlement places them whether the relief shows them or not.
[[nodiscard]] std::vector<PlacedVolcano> placeVolcanoes(const GeographyPlates& plates,
                                                        std::uint64_t worldSeed,
                                                        const VolcanoParams& params, double scale,
                                                        int threads);

} // namespace terrain

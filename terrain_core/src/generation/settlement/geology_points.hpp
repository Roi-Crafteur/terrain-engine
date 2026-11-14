#pragma once

// Internal to the settlement: the points of the geology in data (the arcane craters; the
// volcanoes are the core's, ../volcano_placement.hpp), kept apart by ../point_spacing.hpp.

#include "../point_spacing.hpp"
#include "terrain/generation/settlement/geology.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>
#include <vector>

namespace terrain::settlement {

// The arcane craters of a world, anywhere, the major ones first.
[[nodiscard]] std::vector<ArcaneCrater> placeCraters(const WorldSize& world,
                                                     std::uint64_t worldSeed,
                                                     const GeologyRule& rule, double scale);

} // namespace terrain::settlement

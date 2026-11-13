#pragma once

// The dry basins below the sea level of agent 52, internal to terrain_core: a second family on the
// forms of the depressions of phase 11B (depression_forms.hpp: their warped and lobed outline, the
// escarpment of their steep side, their salt pans and buttes), more of them and smaller, not as
// far inland (the Qattara depression lies 50 km from the sea). At most one per cell of about
// 400 km, where the geography at its center is a low plain (its regional elevation at most
// 400 m), without belt or rift; accepted only where its whole footprint (low_footprint.hpp) lies
// on land without relief (intensity at most 0.11: no mountain, they begin at 0.12 and keep their
// bits), 40 km from the sea at least, clear of the depressions of phase 11B and of the excluded
// zone (agent 46). The climate of the planet level fills them or leaves them dry.
// Pure arithmetic of doubles, noises and hashes: the same bits on every platform, periodic on
// the world.

#include "depressions.hpp"
#include "low_footprint.hpp"
#include "low_reach.hpp"

#include <cstdint>
#include <vector>

namespace terrain::lows {

struct Basins {
    depressions::Forms forms;
    std::vector<depressions::Site> sites;
    ReachLattice reach;
};

// The basins of seed on the geography, clear of the depressions given and of the excluded zone.
[[nodiscard]] Basins makeBasins(std::uint64_t seed, const Geography& geography, double density,
                                const std::vector<depressions::Site>& depressions,
                                const Exclusion& excluded);

// How far the ground of a basin may move from its center, meters.
[[nodiscard]] double basinReach(const depressions::Site& s);

// The basin reaching the canonical (x, z) whose profile there goes the deepest, for ground base
// meters high (a share of 0 without any).
[[nodiscard]] depressions::Basin basinAt(const Basins& basins, const WorldSize& world, double x,
                                         double z, double base);

} // namespace terrain::lows

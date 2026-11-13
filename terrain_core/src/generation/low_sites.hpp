#pragma once

// The sites of the lows of agent 52 (terrain/generation/lows.hpp), internal to terrain_core: the
// families the Heightfield lays after the depressions of phase 11B, built once from the seed and
// the geography, each with its own stream of seeds.

#include "impact_craters.hpp"
#include "low_basins.hpp"
#include "plain_hollows.hpp"
#include "rift_grabens.hpp"
#include "terrain/generation/lows.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace terrain::lows {

struct Sites {
    std::optional<Grabens> grabens;
    std::optional<Basins> basins;
    std::optional<Craters> craters;
    std::optional<Hollows> hollows; // implicit: never listed by listOf
};

// The sites of seed for these parameters over the geography, the basins clear of the depressions,
// the craters clear of every other low, every site out of the excluded zone (low_footprint.hpp);
// the craters worn by the climate (pre_climate.hpp; a mean humidity without), the hollows of the
// plains set by it (none without).
[[nodiscard]] Sites makeSites(std::uint64_t seed, const LowsParams& params,
                              const Geography& geography, const PreClimate* climate,
                              const std::vector<depressions::Site>& depressions,
                              const Exclusion& excluded);

// A depression of phase 11B as a low: its reach that of depressions.cpp.
[[nodiscard]] LowSite siteOf(const depressions::Site& s);

// Whether a low of these families may reach the canonical (x, z): false far from all (cheap).
[[nodiscard]] bool mayReach(const Sites& sites, double x, double z);
// The same for the families that are listed (a lattice lookup): the hollows aside.
[[nodiscard]] bool mayReachListed(const Sites& sites, double x, double z);

// The sites of every family, in their order (grabens, basins, then craters), each in its own
// order.
[[nodiscard]] std::vector<LowSite> listOf(const Sites& sites);

} // namespace terrain::lows

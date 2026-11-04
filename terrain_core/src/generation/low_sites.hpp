#pragma once

// The sites of the lows of agent 52 (terrain/generation/lows.hpp), internal to terrain_core: the
// families the Heightfield lays after the depressions of phase 11B, built once from the seed and
// the geography, each with its own stream of seeds.

#include "low_basins.hpp"
#include "rift_grabens.hpp"
#include "terrain/generation/lows.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace terrain::lows {

struct Sites {
    std::optional<Grabens> grabens;
    std::optional<Basins> basins;
};

// The sites of seed for these parameters over the geography, the basins clear of the depressions.
[[nodiscard]] Sites makeSites(std::uint64_t seed, const LowsParams& params,
                              const Geography& geography,
                              const std::vector<depressions::Site>& depressions);

// Whether a low of these families may reach the canonical (x, z): false far from all (cheap).
[[nodiscard]] bool mayReach(const Sites& sites, double x, double z);

// The sites of every family, in their order (grabens, then basins), each in its own order.
[[nodiscard]] std::vector<LowSite> listOf(const Sites& sites);

} // namespace terrain::lows

#pragma once

// Internal to the settlement: what the maps read of the planet level, once: its land cells, and the
// water of each (its river class, its fresh or salt lake, frozen ground, closed basins).

#include "terrain/generation/planet/planet_land.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/settlement/settlement_config.hpp"

#include <cstdint>
#include <memory>

namespace terrain::settlement {

// The cells of the level that are not the sea (its lakes included).
[[nodiscard]] std::shared_ptr<const planet::LandIndex> landOf(const PlanetCells& cells);

// The bits of SettlementMaps::water on every land cell. The drainage basins of the level are
// computed for the closed basins, then freed.
[[nodiscard]] planet::LandValues<std::uint8_t>
waterFlags(const PlanetLevel& level, const std::shared_ptr<const planet::LandIndex>& land,
           const RiverClasses& rivers, int threads);

} // namespace terrain::settlement

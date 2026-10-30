#pragma once

// Internal to the settlement: the sections of the configuration of the world read by
// settlement_config_parts.cpp.

#include "data_fields.hpp"
#include "terrain/generation/settlement/settlement_config.hpp"

#include <vector>

namespace terrain::settlement {

[[nodiscard]] MagicRule readMagic(const Fields& f);
[[nodiscard]] GeologyRule readGeology(const Fields& f);
[[nodiscard]] HabitabilityRule readHabitability(const Fields& f);
// The models in the order of the file, each id once, at least one.
[[nodiscard]] std::vector<TerrainModel> readTerrainModels(const Fields& f);

} // namespace terrain::settlement

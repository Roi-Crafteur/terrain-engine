#pragma once

// Figures of the lakes of a Planet level, for the checks of the relief (phase 11R bis): how much
// of their area lies in deep basins, the closed hollows of the relief that the level fills.

#include "terrain/generation/planet/planet_level.hpp"

namespace terrain {

// Share of the lake area in lakes at least depth meters deep (their level above their lowest
// cell), in [0, 1]; 0 without lakes.
[[nodiscard]] double deepLakeShare(const PlanetLevel& level, float depth = 500.0f);

} // namespace terrain

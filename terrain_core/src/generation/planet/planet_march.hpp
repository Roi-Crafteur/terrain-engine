#pragma once

#include "terrain/generation/planet/planet_bands.hpp"
#include "terrain/generation/planet/planet_land.hpp"
#include "terrain/generation/planet/planet_params.hpp"

#include <vector>

// Internal to the planet level: the march of the moisture of the climate, band by band.
namespace terrain::planet {

// Moisture and precipitation of every row, band by band (WindBands: each band along its chain of
// rows, the bands in parallel), in place: values holds the orographic surface, and each row is
// replaced by its precipitation once every band weighing on it is marched (the march of a row
// reads its own surface only; a row two bands share keeps the weighted rain of the first one
// apart until the second is done, then takes their sum, the same bits whichever came first).
// The surface of the land cells is kept first, by slot, for the rain of the storms.
void marchBands(const PlanetGrid& grid, std::vector<float>& values, const LandIndex& land,
                const std::vector<float>& rowTemperature, const PlanetClimateParams& params,
                const WindBands& bands, int threads, std::vector<float>& landSurface);

} // namespace terrain::planet

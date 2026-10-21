#pragma once

#include "terrain/generation/planet/planet_params.hpp"
#include "terrain/generation/planet/planet_wind.hpp"

#include <cstdint>
#include <vector>

namespace terrain::planet {

// Inputs of the moisture march along one row of the grid.
struct MoistureRow {
    const float* height = nullptr;         // ground under the air: the smoothed relief of the row
    const std::uint8_t* ocean = nullptr;   // ocean mask of the row (OceanKind)
    const float* upwindMoisture = nullptr; // moisture leaving the upwind row along z, or nullptr
    const float* upwindAir = nullptr;      // altitude of the air leaving it (with upwindMoisture)
    int nx = 0;
    double cellSize = 1.0;
    float temperature = 0.0f; // sea-level temperature of the row
    double latitude = 0.0;    // degrees
    PlanetWind wind;
};

// What leaves each cell of the row: moisture (mm/yr x m), altitude of the air (m), and the
// precipitation of the cell (mm/yr).
struct MoistureOutput {
    std::vector<float> moisture;
    std::vector<float> air;
    std::vector<float> precipitation;
};

// Steady advection of the moisture along the wind, first-order upwind: each cell takes the
// moisture (and the air altitude) leaving its upwind neighbor along x (weight |wind.x|) and
// along z (weight |wind.z|; none from a row at a divergence line, upwindMoisture null, where
// the flow is taken as zonal), loses rain (the rain length, the rise of the ground above the air,
// the capacity of the air at its temperature), then gains evaporation (toward saturation over
// the ocean, a share of the evapotranspiration over land). The air keeps its altitude above
// the ground, which decays over airDescentLength. The row is a cycle along x: the march starts
// at its first ocean column (or column 0) and goes around 3 times. Values below 1e-20 become
// exactly 0.
void marchMoistureRow(const MoistureRow& row, const PlanetClimateParams& params,
                      MoistureOutput& out);

// Latitude factor of the rain rate (PlanetClimateParams).
[[nodiscard]] double rainFactor(double latitude, const PlanetClimateParams& params);

} // namespace terrain::planet

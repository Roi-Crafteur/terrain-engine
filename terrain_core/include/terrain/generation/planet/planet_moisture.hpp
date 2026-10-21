#pragma once

#include "terrain/generation/planet/planet_params.hpp"
#include "terrain/generation/planet/planet_wind.hpp"

#include <cstdint>
#include <vector>

namespace terrain::planet {

// Inputs of the moisture march along one row of the grid.
struct MoistureRow {
    const float* height = nullptr;         // altitudes of the row
    const std::uint8_t* ocean = nullptr;   // ocean mask of the row (OceanKind)
    const float* upwindHeight = nullptr;   // altitudes of the upwind row along z
    const float* upwindMoisture = nullptr; // moisture leaving that row, or nullptr (descending air)
    int nx = 0;
    double cellSize = 1.0;
    float temperature = 0.0f; // sea-level temperature of the row
    double latitude = 0.0;    // degrees
    PlanetWind wind;
};

// Steady advection of the moisture along the wind, first-order upwind: each cell takes the
// moisture leaving its upwind neighbor along x (weight |wind.x|) and along z (weight |wind.z|),
// loses rain (the rain length, the rise of the ground along the wind, the capacity of the air
// at its temperature), then gains evaporation (toward saturation over the ocean, a share of the
// evapotranspiration over land). The row is a cycle along x: the march starts at its first
// ocean column (or column 0) and goes around 3 times. Writes the moisture leaving each cell
// (mm/yr x m) and its precipitation (mm/yr); values below 1e-20 become exactly 0.
void marchMoistureRow(const MoistureRow& row, const PlanetClimateParams& params,
                      std::vector<float>& moisture, std::vector<float>& precipitation);

// Latitude factor of the rain rate (PlanetClimateParams).
[[nodiscard]] double rainFactor(double latitude, const PlanetClimateParams& params);

} // namespace terrain::planet

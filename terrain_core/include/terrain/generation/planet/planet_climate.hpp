#pragma once

#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_land.hpp"
#include "terrain/generation/planet/planet_params.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace terrain::planet {

// Sea-level mean temperature of a latitude: the curve of ClimateField::latitudeTemperatureAt,
// equator - (equator - pole) s^1.5 with s = sin^2(pi z / periodZ).
[[nodiscard]] float latitudeTemperature(double z, double periodZ,
                                        const PlanetClimateParams& params);

// Mean temperature of a cell of the given altitude in a row whose sea-level temperature is
// rowTemperature: lapse rate above sea level, none over the sea.
[[nodiscard]] inline float cellTemperature(float rowTemperature, float height,
                                           const PlanetClimateParams& params) {
    return rowTemperature - params.lapseRate * (height > 0.0f ? height : 0.0f);
}

// Whether a land cell is frozen (cellWater): its mean temperature below iceTemperature.
[[nodiscard]] inline bool cellFrozen(float rowTemperature, float height,
                                     const PlanetClimateParams& params) {
    return static_cast<double>(cellTemperature(rowTemperature, height, params)) <
           params.iceTemperature;
}

// Potential evapotranspiration (mm/yr) at a mean temperature.
[[nodiscard]] double potentialEvapotranspiration(double temperature,
                                                 const PlanetClimateParams& params);

// Actual evapotranspiration of land (mm/yr), the Turc-Pike curve of the Budyko family:
// P PET / sqrt(P^2 + PET^2), close to min(P, PET) far from P = PET, 0.71 P at P = PET.
[[nodiscard]] double actualEvapotranspiration(double precipitation, double pet);

// Land runoff (mm/yr): P - actualEvapotranspiration, scaled by smoothstep(aridityLow,
// aridityHigh, P / PET), all of P where PET is 0. Never above P.
[[nodiscard]] double landRunoff(double precipitation, double pet,
                                const PlanetClimateParams& params);

// Extra evaporation of a lake surface over the land it covers (mm/yr): PET - (P - runoff),
// at least 0.
[[nodiscard]] inline double lakeEvaporation(double precipitation, double pet, double runoff) {
    const double extra = pet - (precipitation - runoff);
    return extra > 0.0 ? extra : 0.0;
}

// Saturation moisture of the air at a temperature, in mm/yr x m (PlanetClimateParams).
[[nodiscard]] double saturationMoisture(double temperature, const PlanetClimateParams& params);

// The ground the air flows over: the altitude of every cell (0 over the sea) blurred over
// orographyRadius (two box passes along each axis, periodic).
[[nodiscard]] std::vector<float> orographicSurface(const PlanetGrid& grid,
                                                   const std::vector<float>& height,
                                                   const std::vector<std::uint8_t>& ocean,
                                                   const PlanetClimateParams& params, int threads);

// Climate of the grid: precipitation of every cell (mm/yr) and sea-level temperature of every
// row. The moisture is carried along the prevailing winds over the orographic surface
// (planet_moisture), the rain of the eddies is added, then the whole is blurred.
struct PlanetClimate {
    std::vector<float> precipitation;
    std::vector<float> rowTemperature;
};
[[nodiscard]] PlanetClimate computeClimate(const PlanetGrid& grid, const std::vector<float>& height,
                                           const std::vector<std::uint8_t>& ocean,
                                           const PlanetClimateParams& params, int threads);

// Water of a cell, in units of 1e-6 m^3/s (kWaterUnitsPerCubicMeter): the land runoff (all of the
// precipitation on a frozen cell, which moves on as ice), the extra evaporation of a lake
// covering it (0 on a frozen cell) and whether it is frozen. Nothing for an ocean cell.
inline constexpr double kWaterUnitsPerCubicMeter = 1.0e6;
inline constexpr double kSecondsPerYear = 31'557'600.0; // Julian year
struct CellWater {
    std::int64_t runoff = 0;
    std::int64_t lakeEvaporation = 0;
    bool frozen = false;
};
[[nodiscard]] CellWater cellWater(const PlanetGrid& grid, float rowTemperature, float height,
                                  float precipitation, std::uint8_t oceanKind,
                                  const PlanetClimateParams& params);

// The water of every cell, as the hydrology reads it (from the climate, or a table in tests).
using WaterOf = std::function<CellWater(std::size_t cell)>;
// The water of the cells of a grid from its climate (the vectors must outlive the result).
[[nodiscard]] WaterOf climateWater(const PlanetGrid& grid, const std::vector<float>& height,
                                   const std::vector<std::uint8_t>& ocean,
                                   const PlanetClimate& climate, const PlanetClimateParams& params);

// The same from the precipitation of the land cells alone, by land slot (the lean build keeps
// no precipitation of the sea in float).
[[nodiscard]] WaterOf landWater(const PlanetGrid& grid, const std::vector<float>& height,
                                const LandValues<float>& precipitation,
                                const std::vector<float>& rowTemperature,
                                const PlanetClimateParams& params);

// Water units of a flow of mmPerYear over area square meters, rounded to the nearest unit.
[[nodiscard]] std::int64_t waterUnits(double mmPerYear, double area);
// Cubic meters per second of a number of water units.
[[nodiscard]] inline double cubicMeters(std::int64_t units) {
    return static_cast<double>(units) / kWaterUnitsPerCubicMeter;
}

} // namespace terrain::planet

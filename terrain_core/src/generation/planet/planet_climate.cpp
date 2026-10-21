#include "terrain/generation/planet/planet_climate.hpp"

#include "planet_parallel.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/planet/planet_moisture.hpp"
#include "terrain/generation/planet/planet_relief.hpp"
#include "terrain/generation/planet/planet_wind.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace terrain::planet {

namespace {

// Moisture and precipitation of every row, chain by chain (the chains are independent).
std::vector<float> marchAllRows(const PlanetGrid& grid, const std::vector<float>& height,
                                const std::vector<std::uint8_t>& ocean,
                                const std::vector<float>& rowTemperature,
                                const PlanetClimateParams& params, int threads) {
    const MoistureRows rows = moistureRows(grid, params);
    std::vector<float> precipitation(grid.cells());
    const auto chainCount = static_cast<int>(rows.chains.size());
    parallelBlocks(chainCount, 1, threads, [&](int begin, int end) {
        std::vector<float> previous;
        std::vector<float> moisture;
        std::vector<float> rain;
        for (int c = begin; c < end; ++c) {
            for (const int j : rows.chains[static_cast<std::size_t>(c)]) {
                const int up = rows.upwind[static_cast<std::size_t>(j)];
                MoistureRow row;
                row.height = height.data() + grid.index(0, j);
                row.ocean = ocean.data() + grid.index(0, j);
                row.upwindHeight = height.data() + grid.index(0, up);
                row.upwindMoisture =
                    rows.moistureFrom[static_cast<std::size_t>(j)] >= 0 ? previous.data() : nullptr;
                row.nx = grid.nx;
                row.cellSize = grid.cellSize;
                row.temperature = rowTemperature[static_cast<std::size_t>(j)];
                row.latitude = rowLatitude(grid, j);
                row.wind = rowWind(grid, j, params);
                marchMoistureRow(row, params, moisture, rain);
                std::copy(rain.begin(), rain.end(),
                          precipitation.begin() + static_cast<std::ptrdiff_t>(grid.index(0, j)));
                previous.swap(moisture);
            }
        }
    });
    return precipitation;
}

// One [1 2 1] / 4 pass along x then along z, periodic.
void blurPass(const PlanetGrid& grid, std::vector<float>& values, int threads) {
    std::vector<float> along(values.size());
    parallelRows(grid.nz, threads, [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < grid.nx; ++i) {
                const double sum =
                    static_cast<double>(values[grid.index(grid.wrapColumn(i - 1), j)]) +
                    2.0 * values[grid.index(i, j)] + values[grid.index(grid.wrapColumn(i + 1), j)];
                along[grid.index(i, j)] = static_cast<float>(0.25 * sum);
            }
        }
    });
    parallelRows(grid.nz, threads, [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < grid.nx; ++i) {
                const double sum = static_cast<double>(along[grid.index(i, grid.wrapRow(j - 1))]) +
                                   2.0 * along[grid.index(i, j)] +
                                   along[grid.index(i, grid.wrapRow(j + 1))];
                values[grid.index(i, j)] = static_cast<float>(0.25 * sum);
            }
        }
    });
}

} // namespace

float latitudeTemperature(double z, double periodZ, const PlanetClimateParams& params) {
    // The formula of ClimateField::latitudeTemperatureAt, which needs a valid WorldSize.
    const double sine = dmath::sinTurns(wrapCoordinate(z, periodZ) / (2.0 * periodZ));
    const double s = sine * sine;
    const double equator = params.equatorTemperature;
    return static_cast<float>(equator - (equator - params.poleTemperature) * s * std::sqrt(s));
}

double potentialEvapotranspiration(double temperature, const PlanetClimateParams& params) {
    const double above = temperature - params.petZero;
    return above > 0.0 ? params.petRate * above : 0.0;
}

double landRunoff(double precipitation, double pet, const PlanetClimateParams& params) {
    if (precipitation <= 0.0) {
        return 0.0;
    }
    if (pet <= 0.0) {
        return precipitation;
    }
    const auto aridity = static_cast<float>(precipitation / pet);
    const double share = noise::smoothstep(static_cast<float>(params.aridityLow),
                                           static_cast<float>(params.aridityHigh), aridity);
    return (precipitation - actualEvapotranspiration(precipitation, pet)) * share;
}

double actualEvapotranspiration(double precipitation, double pet) {
    if (precipitation <= 0.0 || pet <= 0.0) {
        return 0.0;
    }
    return precipitation * pet / std::sqrt(precipitation * precipitation + pet * pet);
}

double saturationMoisture(double temperature, const PlanetClimateParams& params) {
    return params.coastPrecipitation * dmath::exp(params.moistureGrowth * (temperature - 15.0)) *
           params.rainLength;
}

PlanetClimate computeClimate(const PlanetGrid& grid, const std::vector<float>& height,
                             const std::vector<std::uint8_t>& ocean,
                             const PlanetClimateParams& params, int threads) {
    const int workers = resolveThreads(threads);
    PlanetClimate climate;
    climate.rowTemperature.resize(static_cast<std::size_t>(grid.nz));
    for (int j = 0; j < grid.nz; ++j) {
        climate.rowTemperature[static_cast<std::size_t>(j)] =
            latitudeTemperature(grid.centerZ(j), grid.periodZ(), params);
    }
    climate.precipitation =
        marchAllRows(grid, height, ocean, climate.rowTemperature, params, workers);
    for (int pass = 0; pass < params.blurPasses; ++pass) {
        blurPass(grid, climate.precipitation, workers);
    }
    return climate;
}

std::int64_t waterUnits(double mmPerYear, double area) {
    const double units = mmPerYear * 1.0e-3 * area / kSecondsPerYear * kWaterUnitsPerCubicMeter;
    return units > 0.0 ? static_cast<std::int64_t>(units + 0.5) : 0;
}

CellWater cellWater(const PlanetGrid& grid, float rowTemperature, float height, float precipitation,
                    std::uint8_t oceanKind, const PlanetClimateParams& params) {
    CellWater water;
    if (oceanKind != kLand) {
        return water;
    }
    const double temperature = cellTemperature(rowTemperature, height, params);
    water.frozen = temperature < params.iceTemperature;
    if (water.frozen) {
        water.runoff = waterUnits(precipitation, grid.cellArea());
        return water;
    }
    const double pet = potentialEvapotranspiration(temperature, params);
    const double runoff = landRunoff(precipitation, pet, params);
    water.runoff = waterUnits(runoff, grid.cellArea());
    water.lakeEvaporation =
        waterUnits(lakeEvaporation(precipitation, pet, runoff), grid.cellArea());
    return water;
}

WaterOf climateWater(const PlanetGrid& grid, const std::vector<float>& height,
                     const std::vector<std::uint8_t>& ocean, const PlanetClimate& climate,
                     const PlanetClimateParams& params) {
    return [&grid, &height, &ocean, &climate, &params](std::size_t cell) {
        const auto row = static_cast<std::size_t>(grid.row(cell));
        return cellWater(grid, climate.rowTemperature[row], height[cell],
                         climate.precipitation[cell], ocean[cell], params);
    };
}

} // namespace terrain::planet

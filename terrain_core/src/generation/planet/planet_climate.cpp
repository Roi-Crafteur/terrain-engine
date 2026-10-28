#include "terrain/generation/planet/planet_climate.hpp"

#include "planet_blur.hpp"
#include "planet_parallel.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/planet/planet_land.hpp"
#include "terrain/generation/planet/planet_moisture.hpp"
#include "terrain/generation/planet/planet_relief.hpp"
#include "terrain/generation/planet/planet_wind.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace terrain::planet {

namespace {

// Moisture and precipitation of every row, chain by chain (the chains are independent), in
// place: values holds the orographic surface, each row replaced by its precipitation once
// marched (the march of a row reads its own surface only). The surface of the land cells is
// kept first, by slot, for the rain of the storms.
void marchInPlace(const PlanetGrid& grid, std::vector<float>& values, const LandIndex& land,
                  const std::vector<float>& rowTemperature, const PlanetClimateParams& params,
                  int threads, std::vector<float>& landSurface) {
    const MoistureRows rows = moistureRows(grid, params);
    landSurface.assign(land.landCells(), 0.0f);
    const auto chainCount = static_cast<int>(rows.chains.size());
    parallelBlocks(chainCount, 1, threads, [&](int begin, int end) {
        MoistureOutput previous;
        MoistureOutput out;
        std::vector<std::uint8_t> ocean(static_cast<std::size_t>(grid.nx)); // of the row
        for (int c = begin; c < end; ++c) {
            for (const int j : rows.chains[static_cast<std::size_t>(c)]) {
                const bool fed = rows.moistureFrom[static_cast<std::size_t>(j)] >= 0;
                const std::size_t first = grid.index(0, j);
                for (std::size_t i = 0; i < ocean.size(); ++i) {
                    ocean[i] = land.land(first + i) ? kLand : kOcean;
                }
                MoistureRow row;
                row.height = values.data() + first;
                row.ocean = ocean.data();
                row.upwindMoisture = fed ? previous.moisture.data() : nullptr;
                row.upwindAir = fed ? previous.air.data() : nullptr;
                row.nx = grid.nx;
                row.cellSize = grid.cellSize;
                row.temperature = rowTemperature[static_cast<std::size_t>(j)];
                row.latitude = rowLatitude(grid, j);
                row.wind = rowWind(grid, j, params);
                marchMoistureRow(row, params, out);
                for (std::size_t cell = first; cell < first + static_cast<std::size_t>(grid.nx);
                     ++cell) {
                    if (land.land(cell)) {
                        landSurface[land.slot(cell)] = values[cell];
                    }
                }
                std::copy(out.precipitation.begin(), out.precipitation.end(),
                          values.begin() + static_cast<std::ptrdiff_t>(first));
                std::swap(previous, out);
            }
        }
    });
}

// Adds the rain of the storms over land: the saturation moisture of the nearby sea cells,
// blurred (stormMoistureRows), faded with the surface of each land cell (by slot).
void addEddyRain(const PlanetGrid& grid, const std::vector<float>& landSurface,
                 const LandIndex& land, const std::vector<float>& rowTemperature,
                 const PlanetClimateParams& params, std::vector<float>& precipitation) {
    const int radius = std::min({static_cast<int>(std::floor(params.eddyRadius / grid.cellSize)),
                                 (grid.nx - 1) / 2, (grid.nz - 1) / 2});
    if (params.eddyShare <= 0.0 || radius <= 0) {
        return;
    }
    std::vector<double> rowMoisture(static_cast<std::size_t>(grid.nz));
    for (int j = 0; j < grid.nz; ++j) {
        rowMoisture[static_cast<std::size_t>(j)] =
            saturationMoisture(rowTemperature[static_cast<std::size_t>(j)], params);
    }
    stormMoistureRows(grid, land, rowMoisture, radius, [&](int j, const std::vector<double>& sea) {
        const double rate =
            params.eddyShare * rainFactor(rowLatitude(grid, j), params) / params.rainLength;
        for (int i = 0; i < grid.nx; ++i) {
            const std::size_t c = grid.index(i, j);
            if (!land.land(c)) {
                continue; // over the sea the advection already brings the moisture back
            }
            const double fade = dmath::exp(-static_cast<double>(landSurface[land.slot(c)]) /
                                           params.eddyHeight);
            const double rain = sea[static_cast<std::size_t>(i)] * rate * fade;
            precipitation[c] += rain < 1e-20 ? 0.0f : static_cast<float>(rain);
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

std::vector<float> orographicSurface(const PlanetGrid& grid, const std::vector<float>& height,
                                     const LandIndex& land, const PlanetClimateParams& params,
                                     int threads) {
    std::vector<float> surface(grid.cells());
    for (std::size_t c = 0; c < grid.cells(); ++c) {
        surface[c] = !land.land(c) || height[c] < 0.0f ? 0.0f : height[c];
    }
    const int radius = static_cast<int>(std::floor(params.orographyRadius / grid.cellSize));
    if (radius > 0) {
        const int workers = resolveThreads(threads);
        boxPassInPlace(grid, surface, radius, workers);
        boxPassInPlace(grid, surface, radius, workers);
    }
    return surface;
}

PlanetClimate computeClimate(const PlanetGrid& grid, const std::vector<float>& height,
                             const std::vector<std::uint8_t>& ocean,
                             const PlanetClimateParams& params, int threads) {
    return computeClimate(grid, height, LandIndex(ocean), params, threads);
}

PlanetClimate computeClimate(const PlanetGrid& grid, const std::vector<float>& height,
                             const LandIndex& land, const PlanetClimateParams& params,
                             int threads) {
    const int workers = resolveThreads(threads);
    PlanetClimate climate;
    climate.rowTemperature.resize(static_cast<std::size_t>(grid.nz));
    for (int j = 0; j < grid.nz; ++j) {
        climate.rowTemperature[static_cast<std::size_t>(j)] =
            latitudeTemperature(grid.centerZ(j), grid.periodZ(), params);
    }
    // One grid: the orographic surface, then the precipitation in its place.
    climate.precipitation = orographicSurface(grid, height, land, params, workers);
    std::vector<float> landSurface;
    marchInPlace(grid, climate.precipitation, land, climate.rowTemperature, params, workers,
                 landSurface);
    addEddyRain(grid, landSurface, land, climate.rowTemperature, params, climate.precipitation);
    freeVector(landSurface);
    for (int pass = 0; pass < params.blurPasses; ++pass) {
        blurPassInPlace(grid, climate.precipitation, workers);
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
    water.frozen = cellFrozen(rowTemperature, height, params);
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

WaterOf landWater(const PlanetGrid& grid, const std::vector<float>& height,
                  const LandValues<float>& precipitation, const std::vector<float>& rowTemperature,
                  const PlanetClimateParams& params) {
    return [&grid, &height, &precipitation, &rowTemperature, &params](std::size_t cell) {
        const LandIndex& land = precipitation.index();
        if (!land.land(cell)) {
            return CellWater{};
        }
        const auto row = static_cast<std::size_t>(grid.row(cell));
        return cellWater(grid, rowTemperature[row], height[cell],
                         precipitation.values()[land.slot(cell)], kLand, params);
    };
}

} // namespace terrain::planet

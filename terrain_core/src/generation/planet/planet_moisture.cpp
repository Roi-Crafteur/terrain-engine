#include "terrain/generation/planet/planet_moisture.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/planet/planet_climate.hpp"
#include "terrain/generation/planet/planet_relief.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace terrain::planet {

namespace {

constexpr int kLaps = 3;
constexpr double kTiny = 1e-20; // below: exactly 0, so that no subnormal ever reaches a result

double flushTiny(double v) {
    return v < kTiny ? 0.0 : v;
}

double bump(double latitude, double center, double width) {
    const double t = (latitude - center) / width;
    return dmath::exp(-t * t);
}

// Per-row constants of the march.
struct RowConstants {
    double weightX = 1.0;   // share of the air coming along x
    double weightZ = 0.0;   // along z
    double step = 1.0;      // length of the path of the wind across a cell, meters
    double rainShare = 0.0; // share of the moisture rained over a cell of flat ground
    double evaporationShare = 0.0;
    double airKept = 1.0; // share of the height of the air above the ground kept over a cell
    double seaSaturation = 0.0;
    int direction = 1; // +1 when the wind blows toward +x
};

RowConstants rowConstants(const MoistureRow& row, const PlanetClimateParams& params) {
    RowConstants k;
    const double ax = std::abs(row.wind.x);
    const double az = std::abs(row.wind.z);
    if (row.upwindMoisture != nullptr) {
        k.weightX = ax / (ax + az);
        k.weightZ = az / (ax + az);
    }
    k.step = row.cellSize / std::max(ax, az);
    k.rainShare = -dmath::expm1(-k.step * rainFactor(row.latitude, params) / params.rainLength);
    k.evaporationShare = -dmath::expm1(-k.step / params.evaporationLength);
    k.airKept = dmath::exp(-k.step / params.airDescentLength);
    k.seaSaturation = saturationMoisture(row.temperature, params);
    k.direction = row.wind.x >= 0.0 ? 1 : -1;
    return k;
}

int firstOceanColumn(const MoistureRow& row) {
    for (int i = 0; i < row.nx; ++i) {
        if (row.ocean[i] != kLand) {
            return i;
        }
    }
    return -1;
}

double surface(float height) {
    return height > 0.0f ? static_cast<double>(height) : 0.0;
}

// Air arriving at a cell: its moisture and altitude.
struct Air {
    double moisture = 0.0;
    double altitude = 0.0;
};

// One cell of the march: the air leaving it, and its rain.
struct CellStep {
    Air air;
    double precipitation = 0.0;
};

CellStep stepCell(const MoistureRow& row, const RowConstants& k, int i, Air arriving,
                  const PlanetClimateParams& params) {
    const double ground = surface(row.height[i]);
    const double rise = ground - arriving.altitude;
    const double upliftShare = rise > 0.0 ? -dmath::expm1(-rise / params.upliftHeight) : 0.0;
    double rained = arriving.moisture * (1.0 - (1.0 - k.rainShare) * (1.0 - upliftShare));
    double moisture = arriving.moisture - rained;
    const double temperature = cellTemperature(row.temperature, row.height[i], params);
    const double capacity = params.capacityFactor * saturationMoisture(temperature, params);
    if (moisture > capacity) {
        rained += moisture - capacity;
        moisture = capacity;
    }
    const double precipitation = flushTiny(rained / k.step);
    if (row.ocean[i] != kLand) {
        moisture += (k.seaSaturation - moisture) * k.evaporationShare;
    } else {
        const double pet = potentialEvapotranspiration(temperature, params);
        moisture += params.recycling * actualEvapotranspiration(precipitation, pet) * k.step;
    }
    const double above = std::max(arriving.altitude - ground, 0.0) * k.airKept;
    return {{flushTiny(moisture), ground + above}, precipitation};
}

} // namespace

double rainFactor(double latitude, const PlanetClimateParams& params) {
    const double factor = 1.0 + params.itcz * bump(latitude, 0.0, 8.0) -
                          params.subtropicalDip * bump(latitude, 25.0, 9.0) +
                          params.polarFront * bump(latitude, 52.0, 10.0);
    return std::max(factor, params.minRainFactor);
}

void marchMoistureRow(const MoistureRow& row, const PlanetClimateParams& params,
                      MoistureOutput& out) {
    const RowConstants k = rowConstants(row, params);
    const auto nx = static_cast<std::size_t>(row.nx);
    out.moisture.assign(nx, 0.0f);
    out.air.assign(nx, 0.0f);
    out.precipitation.assign(nx, 0.0f);
    const int ocean = firstOceanColumn(row);
    const int start = ocean >= 0 ? ocean : 0;
    // Before the first lap: air from the sea, or dry air at the ground of the first cell.
    Air leaving{ocean >= 0 ? k.seaSaturation : 0.0, surface(row.height[start])};
    for (int n = 0; n < kLaps * row.nx; ++n) {
        const int i = ((start + k.direction * n) % row.nx + row.nx) % row.nx;
        Air arriving{k.weightX * leaving.moisture, k.weightX * leaving.altitude};
        if (row.upwindMoisture != nullptr) {
            arriving.moisture += k.weightZ * row.upwindMoisture[i];
            arriving.altitude += k.weightZ * row.upwindAir[i];
        }
        const CellStep cell = stepCell(row, k, i, arriving, params);
        leaving = {static_cast<float>(cell.air.moisture), static_cast<float>(cell.air.altitude)};
        out.moisture[static_cast<std::size_t>(i)] = static_cast<float>(cell.air.moisture);
        out.air[static_cast<std::size_t>(i)] = static_cast<float>(cell.air.altitude);
        out.precipitation[static_cast<std::size_t>(i)] = static_cast<float>(cell.precipitation);
    }
}

} // namespace terrain::planet

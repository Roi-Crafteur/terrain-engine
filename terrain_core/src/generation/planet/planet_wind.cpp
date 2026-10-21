#include "terrain/generation/planet/planet_wind.hpp"

#include <cmath>
#include <cstddef>

namespace terrain::planet {

namespace {

int signOf(double v) {
    return v > 0.0 ? 1 : (v < 0.0 ? -1 : 0);
}

} // namespace

double rowLatitude(const PlanetGrid& grid, int row) {
    return 180.0 * std::abs(grid.centerZ(row)) / grid.periodZ();
}

PlanetWind rowWind(const PlanetGrid& grid, int row, const PlanetClimateParams& params) {
    const double latitude = rowLatitude(grid, row);
    double zonal = -1.0;
    double meridional = params.tradeMeridional;
    if (latitude >= 60.0) {
        meridional = params.polarMeridional;
    } else if (latitude >= 30.0) {
        zonal = 1.0;
        meridional = params.westerlyMeridional;
    }
    // Poleward is -z in the north (z < 0) and +z in the south.
    const double poleward = grid.centerZ(row) < 0.0 ? -1.0 : 1.0;
    const double length = std::sqrt(1.0 + meridional * meridional);
    return {zonal / length, poleward * meridional / length};
}

MoistureRows moistureRows(const PlanetGrid& grid, const PlanetClimateParams& params) {
    const auto rows = static_cast<std::size_t>(grid.nz);
    MoistureRows result;
    result.moistureFrom.assign(rows, -1);
    result.upwind.assign(rows, 0);
    std::vector<int> sign(rows);
    for (int j = 0; j < grid.nz; ++j) {
        sign[static_cast<std::size_t>(j)] = signOf(rowWind(grid, j, params).z);
    }
    for (int j = 0; j < grid.nz; ++j) {
        const int s = sign[static_cast<std::size_t>(j)];
        const int up = grid.wrapRow(j - s);
        result.upwind[static_cast<std::size_t>(j)] = up;
        if (s != 0 && up != j && sign[static_cast<std::size_t>(up)] == s) {
            result.moistureFrom[static_cast<std::size_t>(j)] = up;
        }
    }
    // Chains from their heads; a cycle (every row blowing the same way, which the bands never
    // give) is cut at its lowest row.
    std::vector<bool> placed(rows, false);
    for (int pass = 0; pass < 2; ++pass) {
        for (int head = 0; head < grid.nz; ++head) {
            if (placed[static_cast<std::size_t>(head)] ||
                (pass == 0 && result.moistureFrom[static_cast<std::size_t>(head)] >= 0)) {
                continue;
            }
            result.moistureFrom[static_cast<std::size_t>(head)] = -1;
            std::vector<int> chain;
            for (int j = head; !placed[static_cast<std::size_t>(j)];) {
                placed[static_cast<std::size_t>(j)] = true;
                chain.push_back(j);
                const int down = grid.wrapRow(j + sign[static_cast<std::size_t>(j)]);
                if (result.moistureFrom[static_cast<std::size_t>(down)] != j) {
                    break;
                }
                j = down;
            }
            result.chains.push_back(std::move(chain));
        }
    }
    return result;
}

} // namespace terrain::planet

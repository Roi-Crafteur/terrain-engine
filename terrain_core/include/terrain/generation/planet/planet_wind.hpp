#pragma once

#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_params.hpp"

#include <vector>

namespace terrain::planet {

// Latitude of a row of the grid, in degrees from the equator: 180 |z| / periodZ, so that
// sin^2 of it is the s = sin^2(pi z / world.z) of the climate. 0 on the equator (z = 0), 90 on
// the polar line.
[[nodiscard]] double rowLatitude(const PlanetGrid& grid, int row);

// Unit direction toward which the prevailing wind of a row blows (+x east, +z south), from
// the band of its latitude (PlanetClimateParams). The northern hemisphere is z < 0.
struct PlanetWind {
    double x = 0.0;
    double z = 0.0;
};
[[nodiscard]] PlanetWind rowWind(const PlanetGrid& grid, int row,
                                 const PlanetClimateParams& params);

// Order in which the rows carry the moisture. A row takes the moisture of its upwind row
// along z only if the wind of that row blows toward it: at the divergence lines (30 degrees
// and the poles) neither row feeds the other, which cuts every cycle; the rows then form
// chains, from a divergence line to a convergence line (the equator, 60 degrees).
struct MoistureRows {
    std::vector<int> moistureFrom;        // upwind row whose moisture a row takes, or -1
    std::vector<std::vector<int>> chains; // each chain in order, the head first
};
[[nodiscard]] MoistureRows moistureRows(const PlanetGrid& grid, const PlanetClimateParams& params);

} // namespace terrain::planet

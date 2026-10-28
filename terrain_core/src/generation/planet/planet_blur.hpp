#pragma once

#include "terrain/generation/planet/planet_grid.hpp"

#include <cstdint>
#include <functional>
#include <vector>

// Internal to the planet level: the blurs of the climate without a second grid (phase 11D). Each
// value is the same sum, in the same order, as on whole grids, so the same bits; only the rows in
// flight are kept, a few hundred at most. A grid too small for the window of a pass is blurred
// whole.
namespace terrain::planet {

// One box pass of 2 radius + 1 cells along x then along z, periodic, in place: each value the
// mean of its window in double, rounded to float after each axis.
void boxPassInPlace(const PlanetGrid& grid, std::vector<float>& values, int radius, int threads);

// One [1 2 1] / 4 pass along x then along z, periodic, in place.
void blurPassInPlace(const PlanetGrid& grid, std::vector<float>& values, int threads);

// The moisture of the storms (planet_climate): rowMoisture[j] on the sea cells of row j (ocean
// not kLand), 0 on the land, blurred by two passes of 2 radius + 1 cells along x then along z with
// running sums in double (at least 0), periodic; radius from 1 to (nx - 1) / 2 and (nz - 1) / 2.
// Each row of the result is handed to row(j, values) in increasing order, never kept: two sweeps
// of the rows, and about 4 radius rows in memory.
using StormRow = std::function<void(int row, const std::vector<double>& values)>;
void stormMoistureRows(const PlanetGrid& grid, const std::vector<std::uint8_t>& ocean,
                       const std::vector<double>& rowMoisture, int radius, const StormRow& row);

} // namespace terrain::planet

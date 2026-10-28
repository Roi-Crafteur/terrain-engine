#pragma once

#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_land.hpp"

#include <cstdint>
#include <vector>

namespace terrain::planet {

// The cells sorted in the strict total order of the hydrology (lowerCell: altitude, then index),
// lowest first. Sorted in blocks on up to threads threads, then merged: as no two cells are
// equal, the result is the same whatever the number of threads and the standard library.
[[nodiscard]] std::vector<std::uint32_t> sortCells(const std::vector<float>& height, int threads);
// The land cells alone in that order: what the hydrology walks (it never needs the sea there).
[[nodiscard]] std::vector<std::uint32_t> sortLandCells(const std::vector<float>& height,
                                                       const LandIndex& land, int threads);

// Receiver of every land cell: the direction (index in kPlanetNeighbors) of its steepest descent
// (drop / distance, sqrt(2) on the diagonals) among its neighbors lower in the total order, the
// first such neighbor in that order on a tie. kNoNeighbor for the ocean (a sink) and for the
// pits, the land cells without a lower neighbor. Every receiver is lower: no cycle.
[[nodiscard]] std::vector<std::uint8_t> computeReceivers(const PlanetGrid& grid,
                                                         const std::vector<float>& height,
                                                         const std::vector<std::uint8_t>& ocean,
                                                         int threads);

} // namespace terrain::planet

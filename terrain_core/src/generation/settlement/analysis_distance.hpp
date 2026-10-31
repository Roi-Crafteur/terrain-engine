#pragma once

// Internal to the settlement: distances on the planet grid, exact between the centers of the
// cells up to kMaxDistanceCells, on the torus (the polar line included): a separable transform in
// whole numbers, columns then rows in fixed blocks, so the same bits whatever the threads.

#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_land.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace terrain::settlement {

// columns: one byte per cell of the grid, 0 on the sources, anything else elsewhere; replaced by
// the distance in cells to the nearest source of its column (kMaxDistanceCells + 1 beyond).
void columnDistances(const PlanetGrid& grid, std::vector<std::uint8_t>& columns, int threads);

// The distance in km of each land cell to the nearest source, rounded (kFarDistance beyond
// kMaxDistanceCells cells), from the distances along the columns.
[[nodiscard]] planet::LandValues<std::uint8_t>
landDistances(const PlanetGrid& grid, const std::shared_ptr<const planet::LandIndex>& land,
              const std::vector<std::uint8_t>& columns, int threads);

// The squared distance in cells of every cell to the nearest source (kMaxDistanceCells + 1
// squared beyond): the same transform, for the sea cells of the straits.
[[nodiscard]] std::vector<std::uint16_t> squaredDistances(const PlanetGrid& grid,
                                                          const std::vector<std::uint8_t>& columns,
                                                          int threads);

} // namespace terrain::settlement

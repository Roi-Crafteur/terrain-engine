#pragma once

#include "terrain/generation/planet/planet_grid.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace terrain {

// The rivers cut through the sills of the shallow closed depressions of the planet level
// (phase 11B) instead of filling them: the relief is a noise without erosion, full of hollows a
// few meters to a few tens of meters deep at the scale of the cells, and a river carves its bed
// through such a sill (lakes only fill the real basins). Depths in meters, areas in m^2.
struct PlanetBreachParams {
    // A depression is breached when its depth (its fill level above its lowest cell) is at most
    // depthLow where its outlet drains areaLow or less, up to depthHigh where it drains areaHigh
    // or more (in between, on the logarithm of the area): the larger the river, the deeper it
    // cuts. Under the mountains (its outlet or a cell of it), up to mountainDepth: the rivers carve
    // their valleys through the closed valleys of the mountains (phase 11B), a deeper sill holds
    // a lake of a high basin, to its spill level.
    double depthLow = 15.0;
    double depthHigh = 60.0;
    double areaLow = 1.0e9;  // 1,000 km^2
    double areaHigh = 1.0e11; // 100,000 km^2
    double mountainDepth = 300.0;
};

// What breachDepressions did.
struct PlanetBreach {
    std::size_t depressions = 0; // closed depressions of the relief
    std::size_t breached = 0;
    double breachedArea = 0.0; // m^2 of the depressions breached
    double keptArea = 0.0;     // m^2 of the others
    std::size_t cutCells = 0;  // cells lowered
    float deepestCut = 0.0f;   // meters
};

namespace planet {

// Priority-Flood from the ocean (ties broken by index: the same bits on every run) gives every land
// cell its fill level and the area it drains; the cells below their fill level form the closed
// depressions, one per outlet (the first cell at its fill level on the way to the ocean). Each
// depression shallow enough for the area its outlet drains (PlanetBreachParams) is breached: from
// each of its pits, the cells on the way to the ocean are lowered below the pit, a millimeter per
// cell, until the way reaches a cell lower still (never below kSeaLevel). The others stay closed,
// for the lakes, and so do those below kSeaLevel (the sea would flow in).
// Single-threaded.
[[nodiscard]] PlanetBreach breachDepressions(const PlanetGrid& grid, std::vector<float>& height,
                                             const std::vector<std::uint8_t>& ocean,
                                             const std::vector<std::uint8_t>& mountain,
                                             const PlanetBreachParams& params);

} // namespace planet

} // namespace terrain

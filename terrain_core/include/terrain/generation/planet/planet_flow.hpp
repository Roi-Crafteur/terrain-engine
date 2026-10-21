#pragma once

#include "terrain/generation/planet/planet_climate.hpp"
#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_lakes.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace terrain::planet {

// Where the water of the land goes once the lakes are known.
struct PlanetFlow {
    std::vector<float> discharge;         // m^3/s leaving each dry land cell, 0 elsewhere
    std::vector<std::int64_t> lakeInflow; // per lake: water routed into it (water units)
    std::int64_t toOcean = 0;             // water reaching the sea
};

// One pass in decreasing total order (every cell after all the cells upstream of it): a dry
// land cell passes its runoff and what it received to its receiver, into the sea or into the
// lake covering it; a dry pit gives it to its playa; a cell under a lake gives its runoff to its
// lake. Each overflowing lake adds its exact outflow at its partner when the pass reaches its
// pass cell, after all its inflows (they all come later in the order than its pass, its partner
// earlier). Exact integer water: the inflow of every lake equals the one of solveLakes.
[[nodiscard]] PlanetFlow routeFlow(const PlanetGrid& grid, const std::vector<std::uint8_t>& ocean,
                                   const std::vector<std::uint8_t>& receiver,
                                   const std::vector<std::uint32_t>& order,
                                   const LakeSolution& lakes, const WaterOf& water);

// The overflowing lakes in decreasing total order of their pass (then by index): the order in
// which a pass in decreasing order meets them.
[[nodiscard]] std::vector<std::int32_t> outflowsByPass(const std::vector<std::uint32_t>& order,
                                                       const std::vector<PlanetLake>& lakes);

// The lake covering a cell, or -1.
using LakeAt = std::function<std::int32_t(std::size_t cell)>;

// The playa of every dry pit that has one (pit cell -> lake), sorted by cell.
[[nodiscard]] std::vector<std::pair<std::uint32_t, std::int32_t>>
playaPits(const std::vector<PlanetLake>& lakes);

inline constexpr std::int32_t kNoBasin = -1;

// Drainage basin of every cell, named after where its water ends: the land cell (or, for a lake
// spilling straight into it, the sea cell) where it reaches the sea; grid.cells() + the index of
// the terminal lake (or playa) where it evaporates; or the cell itself for a dry pit that never
// gets water. kNoBasin for the sea. A lake spilling into another belongs to the basin of the
// lake downstream.
[[nodiscard]] std::vector<std::int32_t> computeBasins(const PlanetGrid& grid,
                                                      const std::vector<std::uint8_t>& ocean,
                                                      const std::vector<std::uint8_t>& receiver,
                                                      const LakeAt& lakeAt,
                                                      const std::vector<PlanetLake>& lakes);

} // namespace terrain::planet

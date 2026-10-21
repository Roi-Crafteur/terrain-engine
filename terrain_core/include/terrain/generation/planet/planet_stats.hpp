#pragma once

#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/planet/planet_rivers.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace terrain {

// A drainage basin, named after where its water ends (planet::computeBasins).
struct PlanetBasinStat {
    std::int32_t basin = -1;
    double area = 0.0;        // square meters
    double frozenShare = 0.0; // of its area
    bool sea = false;         // it reaches the sea (else a terminal lake, a playa, a dry pit)
    double discharge = 0.0;   // m^3/s reaching the sea, or entering its terminal lake
    double x = 0.0;           // its outlet: the mouth cell, or the lowest cell of its lake
    double z = 0.0;
};

// A river from its farthest source to its end, through the confluences and the lakes it
// crosses (measured like the Nile: the longest path).
struct PlanetRiverStat {
    std::int32_t mouthSegment = -1; // its last segment, or -1 when it ends through a lake
    double length = 0.0;            // meters along the polylines (cell to cell: no meanders)
    double lakeLength = 0.0;        // meters of it straight across the lakes it crosses
    double discharge = 0.0;         // m^3/s at its last river point
    RiverMouth mouth = RiverMouth::Sea;
    std::uint8_t strahler = 1;
    double x = 0.0; // its last river point (or the outlet of its last lake)
    double z = 0.0;
};

// Figures to compare worlds.
struct PlanetStats {
    double landShare = 0.0;         // of the cells
    std::vector<double> landMasses; // areas of the 8-connected land masses, largest first, m^2
    std::vector<PlanetBasinStat> largestBasins;
    std::vector<PlanetRiverStat> longestRivers;
    std::size_t lakes = 0;
    std::size_t overflowingLakes = 0;
    std::size_t terminalLakes = 0; // with water, without outlet
    std::size_t playas = 0;
    double lakeArea = 0.0;    // square meters under the lakes
    double riverLength = 0.0; // all segments, meters
};

namespace planet {

// Areas of the 8-connected masses of non-ocean cells on the torus (lakes are land), largest
// first, then by first cell.
[[nodiscard]] std::vector<double> landMasses(const PlanetGrid& grid,
                                             const std::vector<std::uint8_t>& isOcean);

// The top longest rivers of a graph: longest paths from a source to an end (the sea, a terminal
// lake, a playa, the ice, or a lake letting out less than minDischarge m^3/s or no river),
// through the confluences and the lakes: a river crossing a lake goes straight from where it
// enters to the partner of the lake's pass, then on in a river, another lake or the sea. Longest
// first, then in order.
[[nodiscard]] std::vector<PlanetRiverStat>
longestRivers(const RiverGraph& graph, const std::vector<PlanetLake>& lakes, const PlanetGrid& grid,
              const std::function<std::int32_t(std::size_t)>& lakeAt,
              const std::function<bool(std::size_t)>& isOcean, double minDischarge,
              std::size_t top);

// Every figure of a level; the top largest basins and longest rivers.
[[nodiscard]] PlanetStats planetStats(const PlanetLevel& level, std::size_t top = 5);

} // namespace planet

} // namespace terrain

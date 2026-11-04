#pragma once

// The links of the planet level through the passes (task 11C, step 1).
//
// The receiver of a cell is its steepest neighbor below it, read on the floors of the cells (the
// lowest of a few samples of 8 km cells): two cells of two valleys side by side, one a little
// lower, were linked across the crest between them, and the rivers of 11B cut gorges through the
// ridges. The cells under the mountains now keep their exits (planet_relief.hpp, CellExits: for
// each neighbor, the lowest height a way from the floor point to the side facing it must climb to
// over the samples of the cell), read from the samples their floor was found from, at no cost of
// relief. The climb of a link is the higher of the exit of the cell toward the neighbor and of the
// exit of the neighbor toward the cell (when it has some), above the floor of the cell. A link is
// open when it climbs no more than the tolerance: the water goes down that way without a crest to
// cross. Among its neighbors below it, a cell takes the steepest open one; when none is open (the
// cell lies in a hollow at the scale of the cells: its water must cross a pass anyway), the
// steepest one, as before. Nothing else changes: any neighbor below a cell is a receiver the
// hierarchy, the lakes and the flow accept.
//
// The barrier of a way between two points on a ladder over the relief (linkBarrier) measures the
// crests the rivers cross (the tools and the tests), finer than the exits.
//
// Pure arithmetic: the same links on every platform and thread.

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_land.hpp"
#include "terrain/generation/planet/planet_params.hpp"
#include "terrain/generation/planet/planet_relief.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace terrain::planet {

// A ladder of sections across the straight line between two points, lanes laneSpacing meters
// apart (an odd count: the middle on the line).
struct LinkLadder {
    int sections = 8;
    int lanes = 3;
    double laneSpacing = 1500.0;
};

// Heights of a relief at points (xs[i], zs[i]) into out[i].
using LinkHeights = std::function<void(std::span<const double> xs, std::span<const double> zs,
                                       std::span<float> out)>;

// The barrier of the way from (x0, z0) to (x1, z1) over a relief: the lowest, along the ladder,
// of the highest point of a way from the middle lanes of the first section to those of the last
// one (a way moves at most one lane from a section to the next).
[[nodiscard]] float linkBarrier(const LinkHeights& heights, double x0, double z0, double x1,
                                double z1, const LinkLadder& ladder);
[[nodiscard]] float linkBarrier(const Heightfield& relief, double x0, double z0, double x1,
                                double z1, const LinkLadder& ladder);

// The links that climb more than params.tolerance above the floor of their cell, from the exits
// of the cells (by row, sorted by column, as sampleCells gives them): per land cell, bit k set when its link
// to neighbor k (kPlanetNeighbors) is closed; 0 for the cells without exits. The climb of a link
// is the higher of the exit of the cell toward the neighbor and of the exit of the neighbor toward
// the cell (when it has some), above the floor of the cell.
[[nodiscard]] LandValues<std::uint8_t> closedLinks(const PlanetGrid& grid,
                                                   const std::vector<std::vector<CellExits>>& rows,
                                                   const std::shared_ptr<const LandIndex>& land,
                                                   const PlanetLinkParams& params, int threads);

// What computeReceivers reads of the links: the closed links of a cell (bit k for neighbor k).
struct ReceiverLinks {
    std::function<std::uint8_t(std::size_t cell)> closed;
};

} // namespace terrain::planet

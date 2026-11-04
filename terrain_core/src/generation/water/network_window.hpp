#pragma once

// The window of a block of the fine network (network_block.hpp), internal to terrain_core: the
// nodes of the cells around the cell of the block, side by side, and the ways of its trunks.

#include "network_block.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace terrain::water {

struct NetworkWindow {
    int side = 0;         // nodes per side
    int perCell = 0;      // nodes per side of a cell
    int radius = 1;       // cells around the middle one
    int ci = 0;           // the middle cell
    int cj = 0;
    std::vector<float> height;
    std::vector<std::uint8_t> sea;
    std::vector<float> permeability;
    std::vector<std::uint32_t> cell; // the cell of the planet level of each node

    [[nodiscard]] std::size_t index(int a, int b) const {
        return static_cast<std::size_t>(b) * static_cast<std::size_t>(side) +
               static_cast<std::size_t>(a);
    }
    [[nodiscard]] std::size_t count() const {
        return static_cast<std::size_t>(side) * static_cast<std::size_t>(side);
    }
};

[[nodiscard]] NetworkWindow networkWindow(const PlanetGrid& grid, const CellNodesOf& nodesOf,
                                          int ci, int cj, int radius, int perCell);

// The nodes of the window on the ways of the trunks of the planet network whose two cells lie in
// it (the lowest way between the lowest nodes of the two cells, over their nodes: minimax, then
// the fewest steps): for each such node, the cell of its trunk (0xFFFFFFFF elsewhere) and the next
// node down its way (-1 at its end).
struct TrunkNodes {
    std::vector<std::uint32_t> cell;
    std::vector<std::size_t> next;
};
[[nodiscard]] TrunkNodes trunkNodes(const PlanetNetwork& network, const NetworkWindow& window);

} // namespace terrain::water

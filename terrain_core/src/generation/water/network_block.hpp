#pragma once

// A block of the fine network (task 11C, step 1), internal to terrain_core: the streams of one
// cell of the planet level, found over the window of the 3 x 3 cells around it.
//
// - Outlets: the trunks (the links of the planet network of at least 4 cells or 10 m^3/s, each
//   the lowest way over the lattice between the lowest nodes of its two cells: minimax, then the
//   shortest), the sea, the lakes of the planet level and the edge of the window.
// - Every node drains toward an outlet by a flood from them (priority by the filled level, then
//   the index: a node reached from below follows the way it was reached by, down the slope; a
//   hollow is filled to its sill and its water goes over it, the sill noted for step 2).
// - The area drained and the discharge (the yield of the planet level of its cell); a channel
//   where the area passes the channel head of its climate, ground and slope
//   (runoff_partition.hpp), wet or dry; the order of Strahler of the channels.
// The nodes of the middle cell are kept; the window decides only where their water goes.
// Deterministic: a pure function of the relief, the planet level and the seed.

#include "network_lattice.hpp"
#include "terrain/generation/planet/planet_network.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace terrain::water {

inline constexpr std::int8_t kNoStep = -128;

// The kind of outlet a node is (0 for none).
enum class NodeOutlet : std::uint8_t { None, Trunk, Sea, Lake, Edge };

// A node of the middle cell.
struct NetworkNode {
    std::int8_t downX = kNoStep; // step to the node its water goes to (-1, 0, 1), or kNoStep
    std::int8_t downZ = kNoStep;
    std::int8_t mainX = kNoStep; // step to its largest channel donor, or kNoStep
    std::int8_t mainZ = kNoStep;
    NodeOutlet outlet = NodeOutlet::None;
    std::uint8_t order = 0; // of Strahler, 0 off the channels
    bool channel = false;
    bool wet = false;
    float height = 0.0f;   // the relief
    float filled = 0.0f;   // the level its water stands at (above height in a filled hollow)
    float area = 0.0f;     // m^2 drained
    float discharge = 0.0f; // m^3/s
    float surface = 0.0f;  // the share of the yield that runs off
    float permeability = 0.0f;
    std::uint32_t trunkCell = 0xFFFFFFFFu; // the cell of the trunk a trunk node lies on
};

// The nodes of the middle cell (ci, cj), row by row, perCell^2 of them.
struct NetworkBlock {
    int ci = 0;
    int cj = 0;
    std::vector<NetworkNode> nodes;
    [[nodiscard]] std::size_t bytes() const { return nodes.capacity() * sizeof(NetworkNode); }
};

// The nodes of a cell, as the block reads them (from the cache of the caller).
using CellNodesOf = std::function<std::shared_ptr<const CellNodes>(int ci, int cj)>;

[[nodiscard]] NetworkBlock computeBlock(const PlanetNetwork& network, const NodeLattice& lattice,
                                        const CellNodesOf& nodesOf, int ci, int cj,
                                        int radius = 1);

} // namespace terrain::water

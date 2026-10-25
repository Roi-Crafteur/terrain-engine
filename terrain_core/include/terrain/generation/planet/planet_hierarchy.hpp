#pragma once

#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_passes.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace terrain::planet {

inline constexpr std::uint32_t kNoCell = 0xFFFFFFFFu;
inline constexpr std::int32_t kOceanNode = 0; // the ocean (and the forced outlet), a pseudo node

// A depression of the hierarchy (Barnes, Callaghan and Wickert 2020): a leaf around each pit,
// or the merge of two depressions at the pass where their waters meet. Nodes are created
// bottom-up, so the children of a node always have lower ids.
struct DepressionNode {
    std::int32_t parent = -1;                     // kOceanNode once attached to the ocean
    std::array<std::int32_t, 2> children{-1, -1}; // -1, -1 for a leaf
    std::uint32_t pit = kNoCell;                  // leaf: its lowest cell
    std::uint32_t pass = kNoCell;                 // where it spills into its sibling or the ocean
    std::uint32_t partner = kNoCell;              // lowest neighbor of the pass on the other side
    float level = 0.0f; // its water when it spills: the height of its pass, or of a col
    std::int32_t geolink = -1;                    // leaf (or kOceanNode) the partner drains to

    [[nodiscard]] bool leaf() const { return children[0] < 0; }
};

// The depression hierarchy of the grid, with what each cell belongs to.
struct DepressionHierarchy {
    std::vector<DepressionNode> nodes; // node 0 is the ocean
    // Per cell: the node that held the cell when the flood reached it (kOceanNode for the ocean
    // and for the land joined to it). A cell lies under the water of its own node as soon as that
    // node holds water: the cells of a node are all flooded before its pass.
    std::vector<std::int32_t> own;
    // Per cell: the leaf whose pit its receivers lead to, or kOceanNode for the sea.
    std::vector<std::int32_t> label;
};

// Floods the cells in the total order (order, from sortCells), a Kruskal on the cells with a
// union-find, the ocean joined beforehand. A land cell without flooded neighbor starts a leaf
// (it is a pit: it has no receiver either); next to one flooded set it joins it; next to several
// it merges them at its altitude, two at a time in the order of kPlanetNeighbors, or attaches
// them to the ocean if the ocean is one of them. Every tree ends attached to the ocean (the grid
// is connected). Single-threaded and iterative (a tree can be a chain of 10^5 nodes).
// With cols (planet_passes.hpp), an edge whose col holds the water joins nothing when its cells
// are flooded: once the water reaches the level of the col (between the cells of that height and
// the next), the sets of its two cells merge there (or attach to the ocean), each passing at its
// own cell of the edge toward the other.
[[nodiscard]] DepressionHierarchy buildHierarchy(const PlanetGrid& grid,
                                                 const std::vector<float>& height,
                                                 const std::vector<std::uint8_t>& ocean,
                                                 const std::vector<std::uint32_t>& order,
                                                 const std::vector<std::uint8_t>& receiver,
                                                 const PlanetCols* cols = nullptr);

} // namespace terrain::planet

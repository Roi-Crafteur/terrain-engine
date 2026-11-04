#pragma once

// The lattice of the fine network (task 11C, step 1), internal to terrain_core: nodes every
// kNodeSpacing meters over the whole world, each moved inside its square by a hash (never a grid
// to see), their relief read in batches cell by cell of the planet level (64 x 64 nodes a cell).
// A node is a pure function of the seed and of its indices: the same in every block, thread and
// order.

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/runoff_partition.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace terrain::water {

inline constexpr double kNodeSpacing = 128.0;
inline constexpr double kNodeJitter = 0.3; // of the spacing, each way

// The nodes of the world: their counts along x and z (the spacing divides the sides).
struct NodeLattice {
    std::int64_t nx = 0;
    std::int64_t nz = 0;
    std::uint64_t seed = 0;
    int perCell = 64; // nodes per side of a cell of the planet level

    [[nodiscard]] std::int64_t wrapI(std::int64_t i) const { return ((i % nx) + nx) % nx; }
    [[nodiscard]] std::int64_t wrapJ(std::int64_t j) const { return ((j % nz) + nz) % nz; }
    // The position of node (i, j), meters, canonical (i, j taken modulo the counts).
    [[nodiscard]] std::array<double, 2> position(std::int64_t i, std::int64_t j) const;
};

[[nodiscard]] NodeLattice nodeLattice(const PlanetGrid& grid, std::uint64_t seed);

// What the network reads at the nodes of one cell of the planet level, row by row.
struct CellNodes {
    std::vector<float> height;         // the relief (without the water), meters
    std::vector<std::uint8_t> sea;     // 1 where the geography puts the sea
    std::vector<float> permeability;   // [0, 1] (runoff_partition.hpp)
};

// The nodes of cell (ci, cj) of grid over relief.
[[nodiscard]] CellNodes cellNodes(const Heightfield& relief, const Permeability& ground,
                                  const NodeLattice& lattice, int ci, int cj);

} // namespace terrain::water

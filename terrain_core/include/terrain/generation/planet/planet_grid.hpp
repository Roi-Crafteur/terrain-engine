#pragma once

#include "terrain/world_size.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace terrain {

// The grid of the planet level: nx x nz square cells of cellSize meters that tile the wrapped
// world exactly (WorldSize), one period along each axis. Cell (i, j) is centered on
// (-periodX / 2 + (i + 0.5) cellSize, -periodZ / 2 + (j + 0.5) cellSize): row 0 is the north
// polar edge (z = -periodZ / 2, the same line as the south one), +z is the south. Cells are
// stored row by row, index j * nx + i. Both axes wrap, the polar line included: every cell has
// 8 neighbors.
struct PlanetGrid {
    int nx = 0;
    int nz = 0;
    double cellSize = 1.0;

    [[nodiscard]] std::size_t cells() const {
        return static_cast<std::size_t>(nx) * static_cast<std::size_t>(nz);
    }
    [[nodiscard]] double periodX() const { return nx * cellSize; }
    [[nodiscard]] double periodZ() const { return nz * cellSize; }
    [[nodiscard]] double cellArea() const { return cellSize * cellSize; }

    [[nodiscard]] std::size_t index(int i, int j) const {
        return static_cast<std::size_t>(j) * static_cast<std::size_t>(nx) +
               static_cast<std::size_t>(i);
    }
    [[nodiscard]] int column(std::size_t cell) const {
        return static_cast<int>(cell % static_cast<std::size_t>(nx));
    }
    [[nodiscard]] int row(std::size_t cell) const {
        return static_cast<int>(cell / static_cast<std::size_t>(nx));
    }
    [[nodiscard]] double centerX(int i) const { return -0.5 * periodX() + (i + 0.5) * cellSize; }
    [[nodiscard]] double centerZ(int j) const { return -0.5 * periodZ() + (j + 0.5) * cellSize; }

    [[nodiscard]] int wrapColumn(int i) const { return ((i % nx) + nx) % nx; }
    [[nodiscard]] int wrapRow(int j) const { return ((j % nz) + nz) % nz; }

    // Neighbor k (0 to 7, in the order of kPlanetNeighbors) of a cell, across the seams.
    [[nodiscard]] std::size_t neighbor(std::size_t cell, int k) const;
};

// Offsets of the 8 neighbors in a fixed order, so that ties between neighbors are always broken
// the same way: the 4 sides first, then the diagonals (the order of the phase 4 grids).
struct PlanetNeighbor {
    int dx;
    int dz;
    bool diagonal;
};
inline constexpr std::array<PlanetNeighbor, 8> kPlanetNeighbors = {{
    {1, 0, false},
    {-1, 0, false},
    {0, 1, false},
    {0, -1, false},
    {1, 1, true},
    {-1, 1, true},
    {1, -1, true},
    {-1, -1, true},
}};
inline constexpr double kPlanetDiagonal = 1.4142135623730951; // sqrt(2), exact to the last bit
inline constexpr std::uint8_t kNoNeighbor = 8;                // "no receiver", "no direction"

// The grid of a world: cellSize must divide both sides exactly (every power of two up to
// 65,536 m does) and give at least 3 cells per side (so that the 8 neighbors are distinct) and
// at most 2^30 cells (cell and lake ids fit in 31 bits). Throws std::invalid_argument otherwise,
// or for an invalid world.
[[nodiscard]] PlanetGrid makePlanetGrid(const WorldSize& world, double cellSize);

// Bilinear interpolation weights of the point (x, z) between the 4 nearest cell centers, on the
// torus: cells (i0, j0), (i1, j0), (i0, j1), (i1, j1) with weights (1 - tx)(1 - tz), tx(1 - tz),
// (1 - tx)tz, tx tz. Exact at the centers (t = 0) and continuous across the seams, as x and z
// are brought back into the canonical range first (wrapCoordinate).
struct PlanetBilinear {
    std::array<std::size_t, 4> cells{};
    double tx = 0.0;
    double tz = 0.0;

    // Interpolates the 4 corner values (in the order of cells).
    [[nodiscard]] double blend(double v00, double v10, double v01, double v11) const {
        const double top = v00 + (v10 - v00) * tx;
        const double bottom = v01 + (v11 - v01) * tx;
        return top + (bottom - top) * tz;
    }
};
[[nodiscard]] PlanetBilinear planetBilinear(const PlanetGrid& grid, double x, double z);

// The cell whose square holds (x, z), on the torus.
[[nodiscard]] std::size_t planetCellAt(const PlanetGrid& grid, double x, double z);

} // namespace terrain

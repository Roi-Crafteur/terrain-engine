#include "terrain/generation/planet/planet_grid.hpp"

#include <cmath>
#include <stdexcept>

namespace terrain {

namespace {

// Number of cells of size cellSize in a period, or 0 if it does not divide the period exactly.
int cellsAlong(double period, double cellSize) {
    const double count = period / cellSize;
    if (count != std::floor(count) || count * cellSize != period || count > 2147483647.0) {
        return 0;
    }
    return static_cast<int>(count);
}

} // namespace

std::size_t PlanetGrid::neighbor(std::size_t cell, int k) const {
    const PlanetNeighbor& offset = kPlanetNeighbors[static_cast<std::size_t>(k)];
    return index(wrapColumn(column(cell) + offset.dx), wrapRow(row(cell) + offset.dz));
}

PlanetGrid makePlanetGrid(const WorldSize& world, double cellSize) {
    validate(world);
    if (!std::isfinite(cellSize) || cellSize <= 0.0) {
        throw std::invalid_argument("makePlanetGrid: the cell size must be finite and above 0");
    }
    const int nx = cellsAlong(world.x, cellSize);
    const int nz = cellsAlong(world.z, cellSize);
    if (nx < 3 || nz < 3) {
        throw std::invalid_argument("makePlanetGrid: the cell size must divide both sides of the "
                                    "world exactly, with at least 3 cells per side");
    }
    if (static_cast<double>(nx) * static_cast<double>(nz) > 1073741824.0) {
        throw std::invalid_argument("makePlanetGrid: at most 2^30 cells");
    }
    return {nx, nz, cellSize};
}

PlanetBilinear planetBilinear(const PlanetGrid& grid, double x, double z) {
    // Position in cells from the center of cell (0, 0): exact for every multiple of 2^-k meters
    // the engine samples, as the periods are multiples of the cell size.
    const double u =
        (wrapCoordinate(x, grid.periodX()) + 0.5 * grid.periodX()) / grid.cellSize - 0.5;
    const double v =
        (wrapCoordinate(z, grid.periodZ()) + 0.5 * grid.periodZ()) / grid.cellSize - 0.5;
    const double fu = std::floor(u);
    const double fv = std::floor(v);
    const int i0 = grid.wrapColumn(static_cast<int>(fu));
    const int j0 = grid.wrapRow(static_cast<int>(fv));
    const int i1 = grid.wrapColumn(i0 + 1);
    const int j1 = grid.wrapRow(j0 + 1);
    PlanetBilinear result;
    result.cells = {grid.index(i0, j0), grid.index(i1, j0), grid.index(i0, j1), grid.index(i1, j1)};
    result.tx = u - fu;
    result.tz = v - fv;
    return result;
}

std::size_t planetCellAt(const PlanetGrid& grid, double x, double z) {
    const double u = (wrapCoordinate(x, grid.periodX()) + 0.5 * grid.periodX()) / grid.cellSize;
    const double v = (wrapCoordinate(z, grid.periodZ()) + 0.5 * grid.periodZ()) / grid.cellSize;
    return grid.index(grid.wrapColumn(static_cast<int>(std::floor(u))),
                      grid.wrapRow(static_cast<int>(std::floor(v))));
}

} // namespace terrain

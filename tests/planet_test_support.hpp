#pragma once

// Hand-built grids for the tests of the planet level.

#include "terrain/generation/noise.hpp"
#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_land.hpp"
#include "terrain/generation/planet/planet_order.hpp"
#include "terrain/generation/planet/planet_relief.hpp"
#include "terrain/generation/planet/planet_rivers.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

namespace planet_test {

// Heights of a grid, row by row, and their ocean mask (below-sea components of any size) with
// its land.
struct Terrain {
    terrain::PlanetGrid grid;
    std::vector<float> height;
    std::vector<std::uint8_t> ocean;
    std::shared_ptr<const terrain::planet::LandIndex> land;

    float& at(int i, int j) { return height[grid.index(i, j)]; }
    std::size_t cell(int i, int j) const { return grid.index(i, j); }
    void markOcean(double minArea = 0.0) {
        ocean = terrain::planet::markOcean(grid, height, minArea);
        land = std::make_shared<const terrain::planet::LandIndex>(ocean);
    }
};

inline Terrain flatTerrain(int nx, int nz, float h, double cellSize = 1000.0) {
    Terrain t;
    t.grid = terrain::PlanetGrid{nx, nz, cellSize};
    t.height.assign(t.grid.cells(), h);
    return t;
}

// Pseudo-random heights in [low, high), smoothed by averaging with the 4 sides passes times.
inline Terrain noisyTerrain(int nx, int nz, std::uint64_t seed, float low, float high, int passes) {
    Terrain t = flatTerrain(nx, nz, 0.0f);
    for (std::size_t c = 0; c < t.grid.cells(); ++c) {
        const std::uint64_t bits = terrain::noise::deriveSeed(seed, c);
        t.height[c] = low + (high - low) * static_cast<float>(bits >> 40) / 16777216.0f;
    }
    for (int p = 0; p < passes; ++p) {
        std::vector<float> next(t.height.size());
        for (std::size_t c = 0; c < t.grid.cells(); ++c) {
            float sum = 2.0f * t.height[c];
            for (int k = 0; k < 4; ++k) {
                sum += t.height[t.grid.neighbor(c, k)];
            }
            next[c] = sum / 6.0f;
        }
        t.height.swap(next);
    }
    return t;
}

struct Cell {
    int i;
    int j;
    float h;
};

// A plateau of 2,000 m with the sea at column 0 and the given channel cells.
inline Terrain plateau(int nx, int nz, std::initializer_list<Cell> channels,
                       double cellSize = 1000.0) {
    Terrain t = flatTerrain(nx, nz, 2000.0f, cellSize);
    for (int j = 0; j < nz; ++j) {
        t.at(0, j) = -100.0f;
    }
    for (const Cell& c : channels) {
        t.at(c.i, c.j) = c.h;
    }
    t.markOcean();
    return t;
}

// The hydrology of a hand-built terrain from a table of water, down to the river graph (rivers
// from 100 water units, 1e-4 m^3/s, unless minDischarge says otherwise).
struct Network {
    terrain::planet::LandValues<std::uint8_t> receiver;
    terrain::planet::LakeSolution lakes;
    terrain::planet::PlanetFlow flow;
    terrain::RiverGraph graph;
};

inline Network network(const Terrain& t, const std::vector<terrain::planet::CellWater>& table,
                       double minDischarge = 1.0e-4) {
    const terrain::planet::WaterOf water = [&](std::size_t c) { return table[c]; };
    const std::vector<std::uint32_t> order = terrain::planet::sortCells(t.height, 1);
    Network n;
    n.receiver = terrain::planet::computeReceivers(t.grid, t.height, t.land, 1);
    const auto hierarchy =
        terrain::planet::buildHierarchy(t.grid, t.height, t.land, order, n.receiver);
    n.lakes = terrain::planet::solveLakes(t.grid, t.height, hierarchy, order, water);
    n.flow =
        terrain::planet::routeFlow(t.grid, t.height, t.land, n.receiver, order, n.lakes, water);
    terrain::PlanetRiverParams params;
    params.minDischarge = minDischarge;
    n.graph = terrain::planet::buildRiverGraph(t.grid, t.height, t.land, n.receiver, order,
                                               n.lakes, n.flow, water, params);
    return n;
}

// Runoff of the given cells (units each), nothing elsewhere.
inline std::vector<terrain::planet::CellWater>
sources(const Terrain& t, std::initializer_list<std::pair<int, int>> cells,
        std::int64_t units = 1000) {
    std::vector<terrain::planet::CellWater> table(t.grid.cells());
    for (const auto& [i, j] : cells) {
        table[t.cell(i, j)].runoff = units;
    }
    return table;
}

// An island of 50 m in a sea of -100 m, with two basins (pits at 10 m in (4, 3) and 5 m in
// (7, 3), other cells at 20 m) separated by a ridge of 30 m (columns 5 and 6), whose lowest rim
// cell (40 m) opens to the sea at (2, 4).
inline Terrain twoBasinIsland() {
    Terrain t = flatTerrain(12, 8, -100.0f);
    for (int j = 2; j <= 5; ++j) {
        for (int i = 2; i <= 9; ++i) {
            t.at(i, j) = 50.0f;
        }
    }
    for (const int i : {3, 4, 7, 8}) {
        t.at(i, 3) = 20.0f;
        t.at(i, 4) = 20.0f;
    }
    t.at(4, 3) = 10.0f;
    t.at(7, 3) = 5.0f;
    for (const int i : {5, 6}) {
        t.at(i, 3) = 30.0f;
        t.at(i, 4) = 30.0f;
    }
    t.at(2, 4) = 40.0f;
    t.markOcean();
    return t;
}

} // namespace planet_test

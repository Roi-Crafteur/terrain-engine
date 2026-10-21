#pragma once

// Hand-built grids for the tests of the planet level.

#include "terrain/generation/noise.hpp"
#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_relief.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace planet_test {

// Heights of a grid, row by row, and their ocean mask (below-sea components of any size).
struct Terrain {
    terrain::PlanetGrid grid;
    std::vector<float> height;
    std::vector<std::uint8_t> ocean;

    float& at(int i, int j) { return height[grid.index(i, j)]; }
    std::size_t cell(int i, int j) const { return grid.index(i, j); }
    void markOcean(double minArea = 0.0) {
        ocean = terrain::planet::markOcean(grid, height, minArea);
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

} // namespace planet_test

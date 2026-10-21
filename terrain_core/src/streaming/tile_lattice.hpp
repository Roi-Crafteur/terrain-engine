#pragma once

// Lattice of a streamed tile, shared by terrain_tiles.cpp and cave_tiles.cpp (internal).

#include "terrain/generation/heightfield.hpp"
#include "terrain/meshing/density_grid.hpp"
#include "terrain/streaming/lod.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <set>
#include <vector>

namespace terrain::tile {

// Integer lattice of a tile, in meters: sample (i, j, k) of vertical level n lies at
// (x0 + i * spacing, n * levelHeight + j * spacing, z0 + k * spacing).
struct TileLattice {
    int x0 = 0;
    int z0 = 0;
    int spacing = 1;
    int levelHeight = kChunkCells; // also the side of the tile
};

inline TileLattice latticeOf(const TileKey& key) {
    const auto spacing = static_cast<int>(tileCellMeters(key.lod));
    const int side = spacing * kChunkCells;
    return {key.origin.x * kChunkCells, key.origin.z * kChunkCells, spacing, side};
}

// Ground heights of the kGridSamples x kGridSamples columns of a tile.
struct TileColumns {
    std::vector<float> heights; // x varies fastest
    float minHeight = 0.0f;
    float maxHeight = 0.0f;

    [[nodiscard]] float at(int i, int k) const {
        return heights[static_cast<std::size_t>(k * kGridSamples + i)];
    }
};

// The heights in one batch (Heightfield::heightsAt): neighboring columns share noise cells.
inline TileColumns sampleColumns(const Heightfield& field, const TileLattice& lattice) {
    constexpr auto kCount = static_cast<std::size_t>(kGridSamples * kGridSamples);
    std::vector<double> xs;
    std::vector<double> zs;
    xs.reserve(kCount);
    zs.reserve(kCount);
    for (int k = 0; k < kGridSamples; ++k) {
        for (int i = 0; i < kGridSamples; ++i) {
            xs.push_back(lattice.x0 + i * lattice.spacing);
            zs.push_back(lattice.z0 + k * lattice.spacing);
        }
    }
    TileColumns columns;
    columns.heights.resize(kCount);
    field.heightsAt(xs, zs, columns.heights);
    const auto [low, high] = std::minmax_element(columns.heights.begin(), columns.heights.end());
    columns.minHeight = *low;
    columns.maxHeight = *high;
    return columns;
}

// Levels n whose samples [n * height, (n + 1) * height] meet [low, high].
inline void addLevels(std::set<int>& levels, double low, double high, int height) {
    const auto first = static_cast<int>(std::ceil(low / height)) - 1;
    const auto last = static_cast<int>(std::floor(high / height));
    for (int level = first; level <= last; ++level) {
        levels.insert(level);
    }
}

// Terrain density y - height of the samples of the level whose lowest samples lie at originY.
inline DensityGrid levelDensity(const TileColumns& columns, const TileLattice& lattice,
                                int originY) {
    DensityGrid grid(0.0f);
    for (int k = 0; k < kGridSamples; ++k) {
        for (int j = 0; j < kGridSamples; ++j) {
            const double sampleY = originY + j * lattice.spacing;
            for (int i = 0; i < kGridSamples; ++i) {
                // Same expression as TerrainTiles::densityAt, so the values are identical.
                grid.set(i, j, k, static_cast<float>(sampleY - columns.at(i, k)));
            }
        }
    }
    return grid;
}

} // namespace terrain::tile

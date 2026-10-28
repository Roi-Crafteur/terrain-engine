#pragma once

// Lattice of a streamed tile, shared by terrain_tiles.cpp and cave_tiles.cpp (internal).

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/rock_faces.hpp"
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
    std::vector<float> heights;         // x varies fastest
    std::vector<float> continentalness; // of each column, for the climate
    // The rock of each column in three dimensions (phase 12A), only when a face lies at one of
    // them: empty otherwise, and the tile is that of the relief alone.
    std::vector<RockColumn> rock;
    float minHeight = 0.0f;
    float maxHeight = 0.0f;

    [[nodiscard]] float at(int i, int k) const {
        return heights[static_cast<std::size_t>(k * kGridSamples + i)];
    }
    [[nodiscard]] float continentalnessAt(int i, int k) const {
        return continentalness[static_cast<std::size_t>(k * kGridSamples + i)];
    }
};

// The heights in one batch (Heightfield::samplesAt, the bits of heightsAt): neighboring columns
// share noise cells. With rock, the rock of the columns too (kept only when a face lies there).
inline TileColumns sampleColumns(const Heightfield& field, const TileLattice& lattice,
                                 bool rock = false) {
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
    std::vector<HeightSample> samples(kCount);
    TileColumns columns;
    if (rock) {
        columns.rock.resize(kCount);
        field.samplesAt(xs, zs, samples, columns.rock);
        const bool faces = std::any_of(columns.rock.begin(), columns.rock.end(),
                                       [](const RockColumn& c) { return c.faceCount > 0; });
        if (!faces) {
            columns.rock.clear();
        }
    } else {
        field.samplesAt(xs, zs, samples);
    }
    columns.heights.reserve(kCount);
    columns.continentalness.reserve(kCount);
    for (const HeightSample& sample : samples) {
        columns.heights.push_back(sample.height);
        columns.continentalness.push_back(sample.continentalness);
    }
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

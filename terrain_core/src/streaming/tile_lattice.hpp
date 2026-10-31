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
    // The water over each column (HeightSample::waterLevel), kNoWaterLevel on dry land.
    std::vector<float> waterLevels;
    std::vector<WaterKind> waterKinds; // HeightSample::water of each column
    // The rock of each column in three dimensions (phase 12A), only when a face lies at one of
    // them: empty otherwise, and the tile is that of the relief alone.
    std::vector<RockColumn> rock;
    // With rock, optionally: the ring of the columns one step beyond each side of the tile
    // (sampleRing), for the normals of its sides.
    std::vector<float> ringHeights;
    std::vector<RockColumn> ring;
    // Optionally (sampleColumns with an outside stride): the heights one step beyond the sides
    // every stride columns along them, the neighbors of the texels on the sides (blendTexels),
    // stride s at index (p / s) * 4 + side (side 0 at i = -1, 1 at i = kGridSamples, 2 at k = -1,
    // 3 at k = kGridSamples).
    std::vector<float> outsideHeights;
    float minHeight = 0.0f;
    float maxHeight = 0.0f;

    [[nodiscard]] float at(int i, int k) const {
        return heights[static_cast<std::size_t>(k * kGridSamples + i)];
    }
    [[nodiscard]] float continentalnessAt(int i, int k) const {
        return continentalness[static_cast<std::size_t>(k * kGridSamples + i)];
    }
    [[nodiscard]] float waterAt(int i, int k) const {
        return waterLevels[static_cast<std::size_t>(k * kGridSamples + i)];
    }
};

// The heights in one batch (Heightfield::samplesAt, the bits of heightsAt): neighboring columns
// share noise cells. With rock, the rock of the columns too, kept only when the detail of the
// tile (cells of lattice.spacing meters, rockDetailForLod) draws a face at one of them: otherwise
// every column is the relief alone, and so is the tile. With outsideStride above 0, the heights
// of TileColumns::outsideHeights in the same batch (a sample holds the same bits in any batch).
inline TileColumns sampleColumns(const Heightfield& field, const TileLattice& lattice,
                                 bool rock = false, int outsideStride = 0) {
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
    for (int p = 0; outsideStride > 0 && p < kGridSamples; p += outsideStride) {
        for (int side = 0; side < 4; ++side) {
            const int across = side % 2 == 0 ? -1 : kGridSamples;
            xs.push_back(lattice.x0 + (side < 2 ? across : p) * lattice.spacing);
            zs.push_back(lattice.z0 + (side < 2 ? p : across) * lattice.spacing);
        }
    }
    std::vector<HeightSample> samples(xs.size());
    TileColumns columns;
    if (rock) {
        const RockDetail detail{static_cast<float>(lattice.spacing)};
        columns.rock.resize(xs.size());
        field.samplesAt(xs, zs, samples, columns.rock, kRunCells * detail.cell);
        columns.rock.resize(kCount);
        const bool faces =
            std::any_of(columns.rock.begin(), columns.rock.end(), [&](const RockColumn& c) {
                return std::any_of(c.faces.begin(), c.faces.begin() + c.faceCount,
                                   [&](const RockFace& f) {
                                       return drawnWeight(f, detail) > 0.0f;
                                   });
            });
        if (!faces) {
            columns.rock.clear();
        }
    } else {
        field.samplesAt(xs, zs, samples);
    }
    for (std::size_t n = kCount; n < samples.size(); ++n) {
        columns.outsideHeights.push_back(samples[n].height);
    }
    samples.resize(kCount);
    columns.heights.reserve(kCount);
    columns.continentalness.reserve(kCount);
    columns.waterLevels.reserve(kCount);
    columns.waterKinds.reserve(kCount);
    for (const HeightSample& sample : samples) {
        columns.waterKinds.push_back(sample.water);
        columns.heights.push_back(sample.height);
        columns.continentalness.push_back(sample.continentalness);
        columns.waterLevels.push_back(sample.water == WaterKind::None ? kNoWaterLevel
                                                                      : sample.waterLevel);
    }
    const auto [low, high] = std::minmax_element(columns.heights.begin(), columns.heights.end());
    columns.minHeight = *low;
    columns.maxHeight = *high;
    return columns;
}

// The columns one step beyond the sides of a tile, kGridSamples on each: side 0 at i = -1, 1 at
// i = kGridSamples, 2 at k = -1, 3 at k = kGridSamples, column p of a side at index
// side * kGridSamples + p (k along the sides 0 and 1, i along 2 and 3). Each holds the bits the
// neighboring tile of the same LOD holds there (Heightfield::samplesAt gives a column the same
// bits in any batch).
inline void sampleRing(const Heightfield& field, const TileLattice& lattice,
                       TileColumns& columns) {
    constexpr auto kCount = static_cast<std::size_t>(4 * kGridSamples);
    std::vector<double> xs;
    std::vector<double> zs;
    xs.reserve(kCount);
    zs.reserve(kCount);
    const int s = lattice.spacing;
    for (int side = 0; side < 4; ++side) {
        for (int p = 0; p < kGridSamples; ++p) {
            const int across = side % 2 == 0 ? -1 : kGridSamples;
            xs.push_back(lattice.x0 + (side < 2 ? across : p) * s);
            zs.push_back(lattice.z0 + (side < 2 ? p : across) * s);
        }
    }
    std::vector<HeightSample> samples(kCount);
    columns.ring.resize(kCount);
    field.samplesAt(xs, zs, samples, columns.ring, kRunCells * static_cast<float>(s));
    columns.ringHeights.clear();
    for (const HeightSample& sample : samples) {
        columns.ringHeights.push_back(sample.height);
    }
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

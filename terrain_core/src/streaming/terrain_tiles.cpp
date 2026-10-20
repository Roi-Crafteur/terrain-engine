#include "terrain/streaming/terrain_tiles.hpp"

#include "terrain/generation/height_grid.hpp"
#include "terrain/generation/terrain_surface.hpp"
#include "terrain/meshing/density_grid.hpp"
#include "terrain/meshing/marching_cubes.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <mutex>
#include <set>
#include <utility>

namespace terrain {

namespace {

static_assert(tileChunks(kPlantLod) == 1, "plant tiles must cover a single chunk");

// Integer lattice of a tile, in meters: sample (i, j, k) of vertical level n lies at
// (x0 + i * spacing, n * levelHeight + j * spacing, z0 + k * spacing).
struct TileLattice {
    int x0 = 0;
    int z0 = 0;
    int spacing = 1;
    int levelHeight = kChunkCells; // also the side of the tile
};

TileLattice latticeOf(const TileKey& key) {
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

TileColumns sampleColumns(const Heightfield& field, const TileLattice& lattice) {
    TileColumns columns;
    columns.heights.reserve(static_cast<std::size_t>(kGridSamples * kGridSamples));
    for (int k = 0; k < kGridSamples; ++k) {
        for (int i = 0; i < kGridSamples; ++i) {
            columns.heights.push_back(field.heightAt(lattice.x0 + i * lattice.spacing,
                                                     lattice.z0 + k * lattice.spacing));
        }
    }
    const auto [low, high] = std::minmax_element(columns.heights.begin(), columns.heights.end());
    columns.minHeight = *low;
    columns.maxHeight = *high;
    return columns;
}

// Levels n whose samples [n * height, (n + 1) * height] meet [low, high].
void addLevels(std::set<int>& levels, double low, double high, int height) {
    const auto first = static_cast<int>(std::ceil(low / height)) - 1;
    const auto last = static_cast<int>(std::floor(high / height));
    for (int level = first; level <= last; ++level) {
        levels.insert(level);
    }
}

// Levels holding the ground of the columns or a lattice point an edited block reaches.
std::vector<int> levelsToMesh(const TileColumns& columns, const BlockEdits& edits, int height) {
    std::set<int> levels;
    addLevels(levels, columns.minHeight, columns.maxHeight, height);
    for (const auto* list : {&edits.added, &edits.removed}) {
        for (const BlockCoord& block : *list) {
            addLevels(levels, block.y - kBlockReach, block.y + kBlockReach, height);
        }
    }
    return {levels.begin(), levels.end()};
}

DensityGrid levelDensity(const TileColumns& columns, const TileLattice& lattice, int originY) {
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

// Indices of the samples first + index * spacing within kBlockReach of center.
std::pair<int, int> reachedSamples(int center, int first, int spacing) {
    const double low = std::ceil(static_cast<double>(center - kBlockReach - first) / spacing);
    const double high = std::floor(static_cast<double>(center + kBlockReach - first) / spacing);
    return {std::max(static_cast<int>(low), 0),
            std::min(static_cast<int>(high), kGridSamples - 1)};
}

void applyBlock(DensityGrid& grid, const TileLattice& lattice, int originY, BlockCoord block,
                BlockEdit edit) {
    const auto [i0, i1] = reachedSamples(block.x, lattice.x0, lattice.spacing);
    const auto [j0, j1] = reachedSamples(block.y, originY, lattice.spacing);
    const auto [k0, k1] = reachedSamples(block.z, lattice.z0, lattice.spacing);
    for (int k = k0; k <= k1; ++k) {
        for (int j = j0; j <= j1; ++j) {
            for (int i = i0; i <= i1; ++i) {
                const float edited = editDensity(
                    grid.at(i, j, k), block, edit, lattice.x0 + i * lattice.spacing,
                    originY + j * lattice.spacing, lattice.z0 + k * lattice.spacing);
                grid.set(i, j, k, edited);
            }
        }
    }
}

// Every added block first, then every removed one, as ModificationStore::editedDensity.
void applyBlocks(DensityGrid& grid, const TileLattice& lattice, int originY,
                 const BlockEdits& edits) {
    for (const BlockCoord& block : edits.added) {
        applyBlock(grid, lattice, originY, block, BlockEdit::Added);
    }
    for (const BlockCoord& block : edits.removed) {
        applyBlock(grid, lattice, originY, block, BlockEdit::Removed);
    }
}

// Appends a level meshed in grid units, scaled to meters and lifted by offsetY.
void appendLevel(Mesh& target, const Mesh& part, float scale, float offsetY) {
    const auto first = static_cast<std::uint32_t>(target.vertices.size());
    for (const Vec3f& v : part.vertices) {
        target.vertices.push_back({v.x * scale, v.y * scale + offsetY, v.z * scale});
    }
    target.normals.insert(target.normals.end(), part.normals.begin(), part.normals.end());
    for (const std::uint32_t index : part.indices) {
        target.indices.push_back(first + index);
    }
}

// Plants of the chunk at (x0, z0). The ecology map samples every 4 m at x0 - 2 + 4 i, a lattice
// shared by every chunk, one sample beyond each side so that every plant has the 4 samples
// around it: the plants never depend on the tile, and chunks join without a seam.
std::vector<Plant> placeChunkPlants(const Heightfield& field, const Ecology& ecology, double x0,
                                    double z0) {
    constexpr double kMapMetersPerPixel = 4.0;
    constexpr int kMapSize = static_cast<int>(kChunkSizeMeters / kMapMetersPerPixel) + 2;
    constexpr double kHalfChunk = kChunkSizeMeters / 2.0;
    const HeightmapRegion region{x0 + kHalfChunk, z0 + kHalfChunk, kMapMetersPerPixel, kMapSize};
    const EcologyMap map = ecology.map(surfaceFromHeightfield(field, region));
    const ReliefHeight surface = [&field](double x, double z) { return field.heightAt(x, z); };
    return ecology.placePlants(map, surface,
                               {x0, z0, x0 + kChunkSizeMeters, z0 + kChunkSizeMeters});
}

void sortUnique(std::vector<ChunkCoord>& chunks) {
    std::sort(chunks.begin(), chunks.end(), ChunkOrder{});
    chunks.erase(std::unique(chunks.begin(), chunks.end()), chunks.end());
}

// Chunks whose tiles the modifications change.
void addTouchedChunks(const ModificationStore& store, std::vector<ChunkCoord>& chunks) {
    for (const auto& [coord, chunk] : store.byChunk()) {
        chunks.push_back(coord);
        for (const auto& [block, edit] : chunk.blocks) {
            const std::vector<ChunkCoord> reached = blockChunks(block);
            chunks.insert(chunks.end(), reached.begin(), reached.end());
        }
    }
}

} // namespace

TerrainTiles::TerrainTiles(std::uint64_t seed, const HeightfieldParams& heightfield,
                           const EcologyParams& ecology)
    : seed_(seed), field_(seed, heightfield), ecology_(seed, ecology) {}

TileData TerrainTiles::generate(const TileRequest& request) const {
    const TileLattice lattice = latticeOf(request.key);
    const TileColumns columns = sampleColumns(field_, lattice);
    std::vector<Plant> plants;
    if (request.key.lod == kPlantLod) {
        plants = placeChunkPlants(field_, ecology_, lattice.x0, lattice.z0);
    }
    BlockEdits edits;
    {
        const std::shared_lock lock(mutex_);
        edits = modifications_.blocksIn(lattice.x0 - kBlockReach,
                                        lattice.x0 + lattice.levelHeight + kBlockReach,
                                        lattice.z0 - kBlockReach,
                                        lattice.z0 + lattice.levelHeight + kBlockReach);
        if (request.key.lod == kPlantLod) {
            modifications_.applyPlantEdits(request.key.origin, plants);
        }
    }

    TileData data;
    data.request = request;
    data.originX = lattice.x0;
    data.originZ = lattice.z0;
    data.minHeight = columns.minHeight;
    data.maxHeight = columns.maxHeight;
    data.plants = std::move(plants);
    const std::vector<int> levels = levelsToMesh(columns, edits, lattice.levelHeight);
    const int bottom = levels.front() * lattice.levelHeight; // the ground always has a level
    data.originY = bottom;
    for (const int level : levels) {
        const int originY = level * lattice.levelHeight;
        DensityGrid grid = levelDensity(columns, lattice, originY);
        applyBlocks(grid, lattice, originY, edits);
        stitchSeams(grid, request.seams);
        appendLevel(data.mesh, marchingCubes(grid), static_cast<float>(lattice.spacing),
                    static_cast<float>(originY - bottom));
    }
    return data;
}

float TerrainTiles::densityAt(int x, int y, int z) const {
    const auto density = static_cast<float>(static_cast<double>(y) - field_.heightAt(x, z));
    const std::shared_lock lock(mutex_);
    return modifications_.editedDensity(density, x, y, z);
}

std::vector<ChunkCoord> TerrainTiles::setBlock(BlockCoord block, BlockEdit edit) {
    const std::unique_lock lock(mutex_);
    modifications_.setBlock(block, edit);
    return blockChunks(block);
}

std::vector<ChunkCoord> TerrainTiles::clearBlock(BlockCoord block) {
    const std::unique_lock lock(mutex_);
    return modifications_.clearBlock(block) ? blockChunks(block) : std::vector<ChunkCoord>{};
}

std::vector<ChunkCoord> TerrainTiles::addPlant(const Plant& plant) {
    const std::unique_lock lock(mutex_);
    modifications_.addPlant(plant);
    return {worldToChunk(plant.x, plant.z)};
}

std::vector<ChunkCoord> TerrainTiles::removePlant(const Plant& plant) {
    const std::unique_lock lock(mutex_);
    modifications_.removePlant(plant);
    return {worldToChunk(plant.x, plant.z)};
}

std::vector<ChunkCoord> TerrainTiles::replaceModifications(ModificationStore store) {
    const std::unique_lock lock(mutex_);
    std::vector<ChunkCoord> chunks;
    addTouchedChunks(modifications_, chunks);
    addTouchedChunks(store, chunks);
    modifications_ = std::move(store);
    sortUnique(chunks);
    return chunks;
}

std::optional<BlockEdit> TerrainTiles::blockAt(BlockCoord block) const {
    const std::shared_lock lock(mutex_);
    return modifications_.blockAt(block);
}

ModificationStore TerrainTiles::modifications() const {
    const std::shared_lock lock(mutex_);
    return modifications_;
}

TileGenerator tileGenerator(const TerrainTiles& tiles) {
    return [&tiles](const TileRequest& request) { return tiles.generate(request); };
}

} // namespace terrain

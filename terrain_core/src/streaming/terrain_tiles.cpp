#include "terrain/streaming/terrain_tiles.hpp"

#include "cave_tile.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/height_grid.hpp"
#include "terrain/generation/terrain_surface.hpp"
#include "terrain/meshing/density_grid.hpp"
#include "terrain/meshing/marching_cubes.hpp"
#include "terrain/streaming/cave_tiles.hpp"
#include "tile_lattice.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <mutex>
#include <optional>
#include <set>
#include <utility>

namespace terrain {

namespace {

using tile::TileColumns;
using tile::TileLattice;

static_assert(tileChunks(kPlantLod) == 1, "plant tiles must cover a single chunk");

// Levels holding the ground of the columns, the caves down to caveDepth below it, or a
// lattice point an edited block reaches.
std::vector<int> levelsToMesh(const TileColumns& columns, const BlockEdits& edits, int height,
                              double caveDepth) {
    std::set<int> levels;
    tile::addLevels(levels, columns.minHeight - caveDepth, columns.maxHeight, height);
    for (const auto* list : {&edits.added, &edits.removed}) {
        for (const BlockCoord& block : *list) {
            tile::addLevels(levels, block.y - kBlockReach, block.y + kBlockReach, height);
        }
    }
    return {levels.begin(), levels.end()};
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

// True if the grid holds solid and air samples: otherwise Marching Cubes gives no triangle.
bool crossesSurface(const DensityGrid& grid) {
    bool solid = false;
    bool air = false;
    for (int k = 0; k < kGridSamples; ++k) {
        for (int j = 0; j < kGridSamples; ++j) {
            for (int i = 0; i < kGridSamples; ++i) {
                (grid.at(i, j, k) < kIsoLevel ? solid : air) = true;
            }
        }
    }
    return solid && air;
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
                           const EcologyParams& ecology, const std::optional<CaveParams>& caves)
    : seed_(seed), field_(seed, heightfield), ecology_(seed, ecology) {
    if (caves) {
        caves_.emplace(seed, *caves);
        caveGround_.height = [this](double x, double z) { return field_.heightAt(x, z); };
    }
}

TileData TerrainTiles::generate(const TileRequest& request) const {
    const TileLattice lattice = tile::latticeOf(request.key);
    const TileColumns columns = tile::sampleColumns(field_, lattice);
    // The far tiles carve nothing: they are those of the world without caves.
    const bool carved = caves_ && caveDetailForLod(request.key.lod).components != 0;
    std::vector<Plant> plants;
    if (request.key.lod == kPlantLod) {
        plants = placeChunkPlants(field_, ecology_, lattice.x0, lattice.z0);
        if (carved) {
            tile::dropPlantsOverEntrances(*caves_, caveGround_, lattice, plants);
        }
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
    const double caveDepth = carved ? tile::caveDepthReach(*caves_, lattice) : 0.0;
    const std::vector<int> levels =
        levelsToMesh(columns, edits, lattice.levelHeight, caveDepth);
    const int bottom = levels.front() * lattice.levelHeight; // the ground always has a level
    data.originY = bottom;
    std::optional<tile::CaveTile> caveTile;
    if (carved) {
        caveTile.emplace(*caves_, caveGround_, request, lattice, columns, levels);
    }
    for (const int level : levels) {
        const int originY = level * lattice.levelHeight;
        DensityGrid grid = caveTile ? caveTile->levelDensity(originY)
                                    : tile::levelDensity(columns, lattice, originY);
        applyBlocks(grid, lattice, originY, edits);
        stitchSeams(grid, request.seams);
        if (caveTile && !crossesSurface(grid)) {
            continue; // a level of rock under the ground, between the caves
        }
        appendLevel(data.mesh, marchingCubes(grid), static_cast<float>(lattice.spacing),
                    static_cast<float>(originY - bottom));
    }
    return data;
}

float TerrainTiles::densityAt(int x, int y, int z) const {
    auto density = static_cast<float>(static_cast<double>(y) - field_.heightAt(x, z));
    if (caves_) {
        density = caveDensityAt(*caves_, caveGround_, caveDetailForLod(0), x, y, z);
    }
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

#include "terrain/streaming/terrain_tiles.hpp"

#include "cave_tile.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/height_grid.hpp"
#include "terrain/generation/terrain_surface.hpp"
#include "terrain/meshing/density_grid.hpp"
#include "terrain/meshing/marching_cubes.hpp"
#include "terrain/storage/edit_window.hpp"
#include "terrain/streaming/cave_tiles.hpp"
#include "tile_lattice.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
std::vector<int> levelsToMesh(const TileColumns& columns, const EditWindow& edits, int height,
                              double caveDepth) {
    std::set<int> levels;
    tile::addLevels(levels, columns.minHeight - caveDepth, columns.maxHeight, height);
    // The edited blocks of a section span its edited heights: their levels are those of its
    // lowest and highest blocks and every level between, as block by block.
    for (const auto& [low, high] : edits.editedSpans()) {
        tile::addLevels(levels, low - kBlockReach, high + kBlockReach, height);
    }
    return {levels.begin(), levels.end()};
}

// Every added block first, then every removed one, as ModificationStore::editedDensity.
void applyBlocks(DensityGrid& grid, const TileLattice& lattice, int originY,
                 const EditWindow& edits) {
    const EditLattice samples(edits, lattice.x0, originY, lattice.z0, lattice.spacing,
                              kGridSamples);
    if (samples.empty()) {
        return;
    }
    for (int k = 0; k < kGridSamples; ++k) {
        for (int j = 0; j < kGridSamples; ++j) {
            for (int i = 0; i < kGridSamples; ++i) {
                grid.set(i, j, k, samples.density(grid.at(i, j, k), i, j, k));
            }
        }
    }
}

// True if the edits leave no surface in the level whatever the terrain: every block within
// reach of its samples lies in filled sections of one edit.
bool filledLevel(const EditWindow& edits, const TileLattice& lattice, int originY) {
    const int side = lattice.levelHeight;
    return edits.filled({lattice.x0 - kBlockReach, originY - kBlockReach, lattice.z0 - kBlockReach},
                        {lattice.x0 + side + kBlockReach, originY + side + kBlockReach,
                         lattice.z0 + side + kBlockReach});
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

// Where a level is meshed: its scale to meters, its height over the mesh origin, its lowest
// samples.
struct LevelPlace {
    float scale;
    float offsetY;
    int originY;
};

// The water (lakes and rivers) and the lava of a level of a tile with caves, each meshed over the
// carved density of the level.
void appendFluids(TileData& data, tile::CaveTile& caveTile, const DensityGrid& carved,
                  const LevelPlace& place) {
    const DensityGrid water = caveTile.fluidDensity(tile::CaveFluid::Water, carved, place.originY);
    if (crossesSurface(water)) {
        appendLevel(data.water, marchingCubes(water), place.scale, place.offsetY);
    }
    const DensityGrid lava = caveTile.fluidDensity(tile::CaveFluid::Lava, carved, place.originY);
    if (crossesSurface(lava)) {
        appendLevel(data.lava, marchingCubes(lava), place.scale, place.offsetY);
    }
}

void sortUnique(std::vector<ChunkCoord>& chunks) {
    std::sort(chunks.begin(), chunks.end(), ChunkOrder{});
    chunks.erase(std::unique(chunks.begin(), chunks.end()), chunks.end());
}

// The chunks brought back into the canonical range of the world, sorted, without duplicates.
std::vector<ChunkCoord> canonicalChunks(std::vector<ChunkCoord> chunks, const WorldSize& world) {
    for (ChunkCoord& chunk : chunks) {
        chunk = wrapChunk(chunk, world);
    }
    sortUnique(chunks);
    return chunks;
}

// A coordinate of a block or a lattice point, meters, brought into the canonical range.
std::int32_t canonicalMeter(std::int64_t meters, double period) {
    return static_cast<std::int32_t>(wrapIndex(meters, static_cast<std::int64_t>(period)));
}

BlockCoord canonicalBlock(BlockCoord block, const WorldSize& world) {
    return {canonicalMeter(block.x, world.x), block.y, canonicalMeter(block.z, world.z)};
}

Plant canonicalPlant(Plant plant, const WorldSize& world) {
    plant.x = wrapCoordinate(plant.x, world.x);
    plant.z = wrapCoordinate(plant.z, world.z);
    return plant;
}

// Edited blocks centered in [minX, maxX] x [minZ, maxZ] (an area of any image, smaller than the
// world): the store holds them in the canonical range, the window reads the canonical chunks of
// the area and answers in its frame.
EditWindow blocksAround(const ModificationStore& store, int minX, int maxX, int minZ, int maxZ,
                        const WorldSize& world) {
    return {store, minX, maxX, minZ, maxZ, world};
}

// Chunks whose tiles the modifications change.
void addTouchedChunks(const ModificationStore& store, std::vector<ChunkCoord>& chunks) {
    for (const auto& [coord, chunk] : store.byChunk()) {
        chunks.push_back(coord);
        const std::vector<ChunkCoord> reached = reachedChunks(coord, chunk);
        chunks.insert(chunks.end(), reached.begin(), reached.end());
    }
}

} // namespace

TerrainTiles::TerrainTiles(std::uint64_t seed, const HeightfieldParams& heightfield,
                           const EcologyParams& ecology, const std::optional<CaveParams>& caves,
                           const WorldSize& world)
    : seed_(seed), field_(seed, heightfield, world), ecology_(seed, ecology, world) {
    if (caves) {
        caves_.emplace(seed, *caves, world);
        caveEnvironment_.emplace(field_, ecology_.climate(), ecology_.params().biomes);
        speleothems_.emplace(seed, caves->speleothems, world);
        if (caves->districts) {
            flora_.emplace(seed, caves->districts->flora, world);
        }
        caveGround_.height = [this](double x, double z) { return field_.heightAt(x, z); };
        caveGround_.environment = [this](double x, double z) {
            return caveEnvironment_->at(x, z);
        };
    }
}

TileData TerrainTiles::generate(const TileRequest& asked) const {
    const TileRequest request{wrapTile(asked.key, world()), asked.seams};
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
    EditWindow edits;
    {
        const std::shared_lock lock(mutex_);
        edits =
            blocksAround(modifications_, lattice.x0 - kBlockReach,
                         lattice.x0 + lattice.levelHeight + kBlockReach, lattice.z0 - kBlockReach,
                         lattice.z0 + lattice.levelHeight + kBlockReach, world());
        if (request.key.lod == kPlantLod) {
            dropPlantsOnRemovedGround(edits, plants);
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
    const auto scale = static_cast<float>(lattice.spacing);
    for (const int level : levels) {
        const int originY = level * lattice.levelHeight;
        if (!caveTile && filledLevel(edits, lattice, originY)) {
            continue; // inside a filled or emptied volume: no surface
        }
        DensityGrid grid = caveTile ? caveTile->levelDensity(originY)
                                    : tile::levelDensity(columns, lattice, originY);
        applyBlocks(grid, lattice, originY, edits);
        stitchSeams(grid, request.seams);
        const auto offsetY = static_cast<float>(originY - bottom);
        if (caveTile && caveTile->holdsWater()) {
            appendFluids(data, *caveTile, grid, {scale, offsetY, originY});
        }
        if (caveTile && !crossesSurface(grid)) {
            continue; // a level of rock under the ground, between the caves
        }
        appendLevel(data.mesh, marchingCubes(grid), scale, offsetY);
    }
    if (caveTile) {
        const bool objects = request.key.lod == kPlantLod;
        caveTile->decorate(data, *caveEnvironment_, caves_->params().materials,
                           objects ? &*speleothems_ : nullptr,
                           objects && flora_ ? &*flora_ : nullptr);
    }
    return data;
}

float TerrainTiles::densityAt(int worldX, int y, int worldZ) const {
    const int x = canonicalMeter(worldX, world().x);
    const int z = canonicalMeter(worldZ, world().z);
    auto density = static_cast<float>(static_cast<double>(y) - field_.heightAt(x, z));
    if (caves_) {
        density = caveDensityAt(*caves_, caveGround_, caveDetailForLod(0), x, y, z);
    }
    EditWindow edits;
    {
        const std::shared_lock lock(mutex_);
        edits = blocksAround(modifications_, x - kBlockReach, x + kBlockReach, z - kBlockReach,
                             z + kBlockReach, world());
    }
    // As ModificationStore::editedDensity: every added block first, then every removed one.
    return edits.density(density, x, y, z);
}

std::vector<ChunkCoord> TerrainTiles::setBlock(BlockCoord block, BlockEdit edit) {
    const BlockCoord canonical = canonicalBlock(block, world());
    const std::unique_lock lock(mutex_);
    modifications_.setBlock(canonical, edit);
    return canonicalChunks(blockChunks(canonical), world());
}

std::vector<ChunkCoord> TerrainTiles::clearBlock(BlockCoord block) {
    const BlockCoord canonical = canonicalBlock(block, world());
    const std::unique_lock lock(mutex_);
    return modifications_.clearBlock(canonical) ? canonicalChunks(blockChunks(canonical), world())
                                                : std::vector<ChunkCoord>{};
}

std::vector<ChunkCoord> TerrainTiles::addPlant(const Plant& plant) {
    const Plant canonical = canonicalPlant(plant, world());
    const std::unique_lock lock(mutex_);
    modifications_.addPlant(canonical);
    return {worldToChunk(canonical.x, canonical.z)};
}

std::vector<ChunkCoord> TerrainTiles::removePlant(const Plant& plant) {
    const Plant canonical = canonicalPlant(plant, world());
    const std::unique_lock lock(mutex_);
    modifications_.removePlant(canonical);
    return {worldToChunk(canonical.x, canonical.z)};
}

std::vector<ChunkCoord> TerrainTiles::applyEdit(const VolumeEdit& edit) {
    const std::unique_lock lock(mutex_);
    return applyVolumeEdit(modifications_, edit, world());
}

std::vector<ChunkCoord> TerrainTiles::applyInSequence(std::span<const SequencedEdit> edits) {
    const std::unique_lock lock(mutex_);
    return terrain::applyInSequence(modifications_, edits, world());
}

std::vector<ChunkCoord> TerrainTiles::replaceModifications(ModificationStore store) {
    const std::unique_lock lock(mutex_);
    std::vector<ChunkCoord> chunks;
    addTouchedChunks(modifications_, chunks);
    addTouchedChunks(store, chunks);
    modifications_ = std::move(store);
    return canonicalChunks(std::move(chunks), world());
}

std::optional<BlockEdit> TerrainTiles::blockAt(BlockCoord block) const {
    const BlockCoord canonical = canonicalBlock(block, world());
    const std::shared_lock lock(mutex_);
    return modifications_.blockAt(canonical);
}

ModificationStore TerrainTiles::modifications() const {
    const std::shared_lock lock(mutex_);
    return modifications_;
}

TileGenerator tileGenerator(const TerrainTiles& tiles) {
    return [&tiles](const TileRequest& request) { return tiles.generate(request); };
}

} // namespace terrain

#include "terrain/streaming/terrain_tiles.hpp"

#include "cave_tile.hpp"
#include "column_cache.hpp"
#include "rock_tile.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/height_grid.hpp"
#include "terrain/generation/terrain_surface.hpp"
#include "terrain/generation/water/hydrology.hpp"
#include "terrain/meshing/density_grid.hpp"
#include "terrain/meshing/marching_cubes.hpp"
#include "terrain/storage/edit_window.hpp"
#include "terrain/streaming/cave_tiles.hpp"
#include "tile_blend.hpp"
#include "tile_edits.hpp"
#include "tile_lattice.hpp"
#include "tile_normals.hpp"
#include "tile_sides.hpp"
#include "tile_rivers.hpp"
#include "tile_water.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>

namespace terrain {

namespace {

using tile::TileColumns;
using tile::TileLattice;

static_assert(tileChunks(kPlantLod) == 1, "plant tiles must cover a single chunk");

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

// The mesh of a level of a tile: lit by the rock of the tile, if it has some, then over the edits,
// its normals never against its triangles.
Mesh levelMesh(const DensityGrid& grid, unsigned seams, const std::optional<tile::RockTile>& rock,
               int originY, const TileColumns& columns, int spacing,
               const std::optional<tile::LevelEdits>& edits) {
    Mesh mesh = stitchedMesh(grid, seams);
    if (rock) {
        rock->lightLevel(mesh, grid, originY);
    } else {
        tile::lightSides(mesh, columns, seams, spacing); // with its ring only: the ground alone
    }
    if (edits) {
        tile::lightEdits(mesh, *edits);
    }
    tile::agreeWithTriangles(mesh);
    return mesh;
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

// Drops the plants of a tile standing over an entrance of the caves or on a face of the rock.
void dropPlants(const CaveField* caves, const CaveGround& ground, const TileLattice& lattice,
                const tile::RockTile* rock, std::vector<Plant>& plants) {
    if (caves != nullptr) {
        tile::dropPlantsOverEntrances(*caves, ground, lattice, plants);
    }
    if (rock != nullptr) {
        rock->dropPlants(plants);
    }
}

// Where a level is meshed: its scale to meters, its height over the mesh origin, its lowest
// samples, and the seams of its tile.
struct LevelPlace {
    float scale;
    float offsetY;
    int originY;
    unsigned seams;
};

// The water (lakes and rivers) and the lava of a level of a tile with caves, each meshed over the
// carved density of the level.
void appendFluids(TileData& data, tile::CaveTile& caveTile, const DensityGrid& carved,
                  const LevelPlace& place) {
    const DensityGrid water = caveTile.fluidDensity(tile::CaveFluid::Water, carved, place.originY);
    if (crossesSurface(water)) {
        appendLevel(data.water, stitchedMesh(water, place.seams), place.scale, place.offsetY);
    }
    const DensityGrid lava = caveTile.fluidDensity(tile::CaveFluid::Lava, carved, place.originY);
    if (crossesSurface(lava)) {
        appendLevel(data.lava, stitchedMesh(lava, place.seams), place.scale, place.offsetY);
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

// The rock of a tile whose columns hold faces, in rock (none otherwise): the ring of the columns
// beyond its sides for the normals of its sides (not those of the caves, when carved), through
// cache; the levels and the heights of the tile reach its rock.
void addRockTile(const Heightfield& field, tile::ColumnCache* cache, bool carved,
                 const TileRequest& request, const TileLattice& lattice, TileColumns& columns,
                 std::optional<tile::RockTile>& rock) {
    if (!carved) {
        // The ring of the columns beyond the sides, for the normals of the sides (the rock and the
        // ground alone, tile_sides.hpp), the same the neighbors of the same LOD give there.
        tile::sampleRing(field, lattice, columns, cache);
    }
    if (columns.rock.empty()) {
        return;
    }
    rock.emplace(columns, request, lattice);
    columns.minHeight = rock->bounds().low;
    columns.maxHeight = rock->bounds().high;
}

} // namespace

TerrainTiles::TerrainTiles(std::uint64_t seed, const HeightfieldParams& heightfield,
                           const EcologyParams& ecology, const std::optional<CaveParams>& caves,
                           const WorldSize& world, std::shared_ptr<const Hydrology> hydrology,
                           bool rock, std::size_t columnCache)
    : seed_(seed), rock_(rock),
      field_(Heightfield(seed, heightfield, world).withHydrology(hydrology)),
      ecology_(seed, ecology, world, planetHumidity(hydrology)),
      columns_(std::make_unique<tile::ColumnCache>(columnCache, rock)) {
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

TerrainTiles::~TerrainTiles() = default;

ColumnCacheStats TerrainTiles::columnCache() const {
    return columns_->stats();
}

TileData TerrainTiles::generate(const TileRequest& asked) const {
    const TileRequest request{wrapTile(asked.key, world()), asked.seams, asked.horizon};
    const TileLattice lattice = tile::latticeOf(request.key);
    // The coarser tiles see the relief at the side of their cells (Heightfield::forFootprint, as
    // the far horizon does): the sharp crests they would cut into teeth rounded over their cells.
    const Heightfield field = request.key.lod > 0
                                  ? field_.forFootprint(tileCellMeters(request.key.lod))
                                  : field_;
    TileColumns columns =
        tile::sampleColumns(field, lattice, rock_, kTileBlendStride, columns_.get());
    // The far tiles carve nothing: they are those of the world without caves.
    const bool carved = caves_ && caveDetailForLod(request.key.lod).components != 0;
    std::optional<tile::RockTile> rockTile;
    addRockTile(field, columns_.get(), carved, request, lattice, columns, rockTile);
    TileData data;
    data.request = request;
    data.originX = lattice.x0;
    data.originZ = lattice.z0;
    data.minHeight = columns.minHeight;
    data.maxHeight = columns.maxHeight;
    data.surfaceBlend = tile::blendTexels(field, ecology_, lattice, columns);
    EditWindow edits;
    {
        const std::shared_lock lock(mutex_);
        edits =
            blocksAround(modifications_, lattice.x0 - kBlockReach,
                         lattice.x0 + lattice.levelHeight + kBlockReach, lattice.z0 - kBlockReach,
                         lattice.z0 + lattice.levelHeight + kBlockReach, world());
        if (request.key.lod == kPlantLod) {
            data.plants = placeChunkPlants(field_, ecology_, lattice.x0, lattice.z0);
            dropPlants(carved ? &*caves_ : nullptr, caveGround_, lattice,
                       rockTile ? &*rockTile : nullptr, data.plants);
            dropPlantsOnRemovedGround(edits, data.plants);
            modifications_.applyPlantEdits(request.key.origin, data.plants);
        }
    }
    const double caveDepth = carved ? tile::caveDepthReach(*caves_, lattice) : 0.0;
    const std::vector<int> levels =
        tile::levelsToMesh(columns, edits, lattice.levelHeight, caveDepth);
    const int bottom = levels.front() * lattice.levelHeight; // the ground always has a level
    data.originY = bottom;
    std::optional<tile::CaveTile> caveTile;
    if (carved) {
        caveTile.emplace(*caves_, caveGround_, request, lattice, columns, levels,
                         rockTile ? &*rockTile : nullptr);
    }
    const auto scale = static_cast<float>(lattice.spacing);
    for (const int level : levels) {
        const int originY = level * lattice.levelHeight;
        if (!caveTile && tile::filledLevel(edits, lattice, columns, originY)) {
            continue; // inside a filled or emptied volume: no surface
        }
        DensityGrid grid = caveTile   ? caveTile->levelDensity(originY)
                           : rockTile ? rockTile->levelDensity(originY)
                                      : tile::levelDensity(columns, lattice, originY);
        const std::optional<tile::LevelEdits> levelEdits =
            tile::applyBlocks(grid, lattice, originY, edits);
        stitchSeams(grid, request.seams);
        const auto offsetY = static_cast<float>(originY - bottom);
        if (caveTile && caveTile->holdsWater()) {
            appendFluids(data, *caveTile, grid, {scale, offsetY, originY, request.seams});
        }
        if (caveTile && !crossesSurface(grid)) {
            continue; // a level of rock under the ground, between the caves
        }
        appendLevel(data.mesh,
                    levelMesh(grid, request.seams, rockTile, originY, columns, lattice.spacing,
                              levelEdits),
                    scale, offsetY);
    }
    if (caveTile) {
        const bool objects = request.key.lod == kPlantLod;
        caveTile->decorate(data, *caveEnvironment_, caves_->params().materials,
                           objects ? &*speleothems_ : nullptr,
                           objects && flora_ ? &*flora_ : nullptr);
    }
    if (rockTile) {
        data.rock = rockTile->vertexRock(data.mesh, data.originY);
        data.rockBands = rockTile->vertexBands(data.mesh, data.originY);
    }
    data.waterLevels = tile::vertexWaterLevels(columns, lattice, data.mesh, data.materials);
    tile::addRiverWater(data, field_, lattice, columns);
    return data;
}

float TerrainTiles::densityAt(int worldX, int y, int worldZ) const {
    const int x = canonicalMeter(worldX, world().x);
    const int z = canonicalMeter(worldZ, world().z);
    auto density = static_cast<float>(static_cast<double>(y) - field_.heightAt(x, z));
    if (rock_) {
        density = rockDensity(field_.rockAt(x, z), y, rockDetailForLod(0));
    }
    if (caves_) {
        // As caveDensityAt, over the rock.
        const double px = x;
        const double py = y;
        const double pz = z;
        const float height = caveGround_.height(px, pz);
        CaveSampler sampler(*caves_, caveGround_, caveDetailForLod(0), {px, py, pz, px, py, pz});
        density = sampler.density(sampler.column(px, pz, height), py, density);
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

float TerrainTiles::groundAt(double x, double z) const {
    const RockColumn column = rock_ ? field_.rockAt(x, z) : RockColumn{};
    if (!rock_ || !column.hasRock()) {
        return field_.heightAt(x, z);
    }
    const RockDetail detail = rockDetailForLod(0);
    const RockBounds bounds = rockBounds(column);
    if (column.spire.radius > 0.0f) {
        return tile::spireGroundAt(column, x, z, detail);
    }
    // Down from above the rock in steps, then the last step halved to the surface.
    constexpr double kStep = 0.25;
    constexpr int kHalvings = 24;
    double above = static_cast<double>(bounds.high) + kStep;
    double below = above;
    while (below > static_cast<double>(bounds.low) - kStep) {
        below = above - kStep;
        if (rockDensity(column, below, detail) < kIsoLevel) {
            break;
        }
        above = below;
    }
    for (int i = 0; i < kHalvings; ++i) {
        const double middle = 0.5 * (above + below);
        (rockDensity(column, middle, detail) < kIsoLevel ? below : above) = middle;
    }
    return static_cast<float>(below);
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

EditWindow TerrainTiles::editWindow(int minX, int maxX, int minZ, int maxZ) const {
    const std::shared_lock lock(mutex_);
    return blocksAround(modifications_, minX, maxX, minZ, maxZ, world());
}

TileGenerator tileGenerator(const TerrainTiles& tiles) {
    return [&tiles](const TileRequest& request) { return tiles.generate(request); };
}

} // namespace terrain

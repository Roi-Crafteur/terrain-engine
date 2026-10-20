#pragma once

#include "terrain/chunk_coord.hpp"
#include "terrain/generation/cave_materials.hpp"
#include "terrain/generation/caves.hpp"
#include "terrain/generation/ecology.hpp"
#include "terrain/generation/speleothems.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/storage/modifications.hpp"
#include "terrain/streaming/stream_manager.hpp"

#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <vector>

namespace terrain {

// Plants are only placed on the tiles of this LOD, which cover a single chunk.
inline constexpr int kPlantLod = 0;

// The streamed world: the infinite Heightfield, density y - heightAt(x, z), with the block edits
// of its modifications, meshed tile by tile for a StreamManager. Its relief, rivers and biomes
// are never changed: the modifications only edit blocks and plants.
//
// Caves are optional (none by default, and then every tile is the one of the world without
// caves): with CaveParams, the caves of a CaveField under the heightfield (no river) are carved
// out of the density before the block edits, with the detail of the LOD of each tile
// (caveDetailForLod).
class TerrainTiles {
public:
    explicit TerrainTiles(std::uint64_t seed, const HeightfieldParams& heightfield = {},
                          const EcologyParams& ecology = {},
                          const std::optional<CaveParams>& caves = std::nullopt);

    // Mesh of the vertical levels of the tile holding its ground or a lattice point an edited
    // block reaches: the density is sampled every tileCellMeters(lod) meters on the lattice of
    // the tile, the block edits are applied, the seams are stitched (stitchSeams), and each level
    // is meshed with Marching Cubes. On LOD 0, the plants of the chunk (Ecology::placePlants on
    // the exact heightfield) after the plant edits. Deterministic, and thread-safe: it may run
    // concurrently with the edits.
    //
    // With caves, the levels go down to the deepest caves (maxDepth below the lowest ground,
    // plus the blend and a cell) and the levels without any surface are skipped; on the sides
    // stitched to a coarser tile, the samples that tile holds are carved with its detail; the
    // plants standing over an entrance are dropped. The lakes of the aquifers are meshed into
    // water where the detail carves them (LOD 0 and 1), every vertex gets its material
    // (biome and climate above, depth, nearby lake) and LOD 0 tiles get their speleothems:
    // none of them changes the density.
    [[nodiscard]] TileData generate(const TileRequest& request) const;

    // Density at the lattice point (x, y, z), meters: the generated density with the block
    // edits, the value every tile sampling this point holds before stitching (with caves, every
    // LOD 0 tile). Thread-safe.
    [[nodiscard]] float densityAt(int x, int y, int z) const;

    // Edits, thread-safe with generate. Each returns the chunks whose tiles must be generated
    // again (StreamManager::invalidate).
    std::vector<ChunkCoord> setBlock(BlockCoord block, BlockEdit edit);
    std::vector<ChunkCoord> clearBlock(BlockCoord block);
    std::vector<ChunkCoord> addPlant(const Plant& plant);
    std::vector<ChunkCoord> removePlant(const Plant& plant);
    // Replaces every modification, to load a save: returns the chunks of the old and new ones.
    std::vector<ChunkCoord> replaceModifications(ModificationStore store);

    [[nodiscard]] std::optional<BlockEdit> blockAt(BlockCoord block) const;
    [[nodiscard]] ModificationStore modifications() const; // a copy
    [[nodiscard]] std::uint64_t seed() const { return seed_; }
    [[nodiscard]] const Heightfield& heightfield() const { return field_; }
    [[nodiscard]] const Ecology& ecology() const { return ecology_; }
    // The caves, or nullptr without caves, and the ground they are carved under.
    [[nodiscard]] const CaveField* caves() const { return caves_ ? &*caves_ : nullptr; }
    [[nodiscard]] const CaveGround& caveGround() const { return caveGround_; }

private:
    std::uint64_t seed_;
    Heightfield field_;
    Ecology ecology_;
    std::optional<CaveField> caves_;
    std::optional<CaveEnvironmentField> caveEnvironment_; // reads field_ and ecology_
    std::optional<SpeleothemField> speleothems_;
    CaveGround caveGround_; // reads field_ and caveEnvironment_
    mutable std::shared_mutex mutex_; // guards modifications_
    ModificationStore modifications_;
};

// Generator of a StreamManager over the tiles, which must outlive it.
[[nodiscard]] TileGenerator tileGenerator(const TerrainTiles& tiles);

} // namespace terrain

#pragma once

#include "terrain/chunk_coord.hpp"
#include "terrain/generation/vegetation.hpp"
#include "terrain/storage/block_section.hpp"
#include "terrain/storage/storage_error.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <utility>
#include <vector>

namespace terrain {

// A block of the smooth terrain: the cube of side 1 m centered on the LOD 0 lattice point
// (x, y, z), in meters. Editing it only changes the density of the lattice points within
// kBlockReach of its center (Chebyshev distance): an edit is local, and every chunk and every
// LOD that samples a lattice point sees the same value there.
struct BlockCoord {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;

    friend auto operator<=>(const BlockCoord&, const BlockCoord&) = default;
};

inline constexpr int kBlockReach = 1;

// The chunk storing the edit of a block: the chunk of its center.
[[nodiscard]] ChunkCoord blockChunk(BlockCoord block);

// Chunks with a lattice point the block reaches (chunk n holds the samples 16 n to 16 n + 16 on
// each axis, its borders included): the tiles covering them must be regenerated after an edit
// of the block. Sorted by z, then x.
[[nodiscard]] std::vector<ChunkCoord> blockChunks(BlockCoord block);

// Signed distance from the lattice point (x, y, z) to the cube of the block, in meters.
[[nodiscard]] float blockDistance(BlockCoord block, int x, int y, int z);

// Density at the lattice point (x, y, z) after the edit of one block: unchanged beyond
// kBlockReach of its center.
[[nodiscard]] float editDensity(float density, BlockCoord block, BlockEdit edit, int x, int y,
                                int z);

// Horizontal position of a generated plant, which identifies it: plants come from a
// deterministic lattice, so the same world always places a plant at exactly the same position.
struct PlantSpot {
    double x = 0.0;
    double z = 0.0;

    friend auto operator<=>(const PlantSpot&, const PlantSpot&) = default;
};

// Index of a block in its section (block_section.hpp).
[[nodiscard]] int blockIndex(BlockCoord block);

// Modifications of one chunk.
struct ChunkModifications {
    // Blocks centered in the chunk, by section (sectionOf of their y): no empty section.
    std::map<std::int32_t, BlockSection> sections;
    std::set<PlantSpot> removedPlants; // generated plants standing in the chunk
    std::vector<Plant> addedPlants;    // plants standing in the chunk, in order of addition

    [[nodiscard]] bool empty() const;
    [[nodiscard]] std::size_t blockCount() const;
    // The edited blocks, sorted.
    [[nodiscard]] std::vector<std::pair<BlockCoord, BlockEdit>> blocks(ChunkCoord coord) const;
    friend bool operator==(const ChunkModifications&, const ChunkModifications&) = default;
};

// Chunks with a lattice point an edited block of the chunk coord reaches (blockChunks of each
// block, merged), sorted by z, then x.
[[nodiscard]] std::vector<ChunkCoord> reachedChunks(ChunkCoord coord,
                                                    const ChunkModifications& chunk);

// Edited blocks of an area, each list sorted.
struct BlockEdits {
    std::vector<BlockCoord> added;
    std::vector<BlockCoord> removed;
};

// Chunks ordered by z, then x.
struct ChunkOrder {
    [[nodiscard]] bool operator()(ChunkCoord a, ChunkCoord b) const;
};

// Modifications of the world, stored per chunk: blocks added or removed (by section), generated
// plants removed and plants added. A chunk without modifications holds no entry. Not
// thread-safe.
class ModificationStore {
public:
    void setBlock(BlockCoord block, BlockEdit edit);
    // Gives the block back to the generated terrain. False if it was not edited.
    bool clearBlock(BlockCoord block);
    [[nodiscard]] std::optional<BlockEdit> blockAt(BlockCoord block) const;

    // The section sectionY of a chunk, nullptr if none of its blocks is edited.
    [[nodiscard]] const BlockSection* section(ChunkCoord coord, std::int32_t sectionY) const;
    // Replaces a section of a chunk: an empty section gives its blocks back to the terrain.
    void setSection(ChunkCoord coord, std::int32_t sectionY, BlockSection section);

    // Edited blocks centered in [minX, maxX] x [minZ, maxZ], meters, bounds included.
    [[nodiscard]] BlockEdits blocksIn(int minX, int maxX, int minZ, int maxZ) const;

    // Density at the lattice point (x, y, z) after the edits of the blocks within reach: every
    // added block first, then every removed one, so the order of the edits never matters.
    [[nodiscard]] float editedDensity(float density, int x, int y, int z) const;

    // Adds a plant, stored in the chunk containing (x, z).
    void addPlant(const Plant& plant);
    // Removes a plant: the first added plant equal to it is erased; otherwise the generated
    // plant at its position is recorded as removed.
    void removePlant(const Plant& plant);
    void removeGeneratedPlant(PlantSpot spot);
    [[nodiscard]] bool isRemoved(const Plant& generated) const;
    // Plants of a chunk after the modifications: the removed plants are dropped from the
    // generated ones, then the plants added in the chunk are appended.
    void applyPlantEdits(ChunkCoord chunk, std::vector<Plant>& plants) const;

    // Moves the chunks of other into this store, other left empty: to gather regions loaded
    // apart. Throws std::invalid_argument, changing neither store, if a chunk is in both.
    void merge(ModificationStore&& other);

    [[nodiscard]] const ChunkModifications* chunk(ChunkCoord coord) const;
    [[nodiscard]] const std::map<ChunkCoord, ChunkModifications, ChunkOrder>& byChunk() const {
        return chunks_;
    }
    [[nodiscard]] bool empty() const { return chunks_.empty(); }
    [[nodiscard]] std::size_t blockCount() const;
    [[nodiscard]] std::size_t removedPlantCount() const;
    [[nodiscard]] std::size_t addedPlantCount() const;

    friend bool operator==(const ModificationStore&, const ModificationStore&) = default;

private:
    ChunkModifications& edit(ChunkCoord coord);
    void pruneIfEmpty(ChunkCoord coord);

    std::map<ChunkCoord, ChunkModifications, ChunkOrder> chunks_;
};

// Modification file, little-endian:
//   "TMOD", u32 version, u64 world seed, u32 chunk count, then per chunk (by z, then x):
//   i32 x, i32 z,
//   u32 blocks, per block: i32 x, i32 y, i32 z, u8 edit,
//   u32 removed plants, per plant: f64 x, f64 z,
//   u32 added plants, per plant: u8 type, f64 x, f64 z, f32 y, f32 scale, f32 rotation,
//   and last u32 FNV-1a of every byte before it.
// The blocks and the removed plants of a chunk are in increasing order, the added plants in
// order of addition. The same modifications always give the same bytes. The world format
// (world_save.hpp) replaces this file; migrateModificationFile converts it. It holds the blocks
// one by one: at most kMaxModificationFileBlocks of them.
inline constexpr std::uint32_t kModificationFileVersion = 1;
inline constexpr std::size_t kMaxModificationFileBlocks = std::size_t{1} << 24;

struct SavedModifications {
    std::uint64_t seed = 0;
    ModificationStore store;
};

// 32-bit FNV-1a.
[[nodiscard]] std::uint32_t fnv1a(std::span<const std::uint8_t> bytes);

// Throws a StorageError (Invalid) beyond kMaxModificationFileBlocks blocks: save a world.
[[nodiscard]] std::vector<std::uint8_t> serializeModifications(const ModificationStore& store,
                                                               std::uint64_t seed);

// Throws a StorageError (a std::runtime_error) unless bytes are a whole modification file:
// magic, version and checksum, no truncated or trailing bytes, valid edits, plant types and
// finite values, chunks, blocks and removed plants in order, no empty chunk, and every
// modification stored in its own chunk.
[[nodiscard]] SavedModifications deserializeModifications(std::span<const std::uint8_t> bytes);

// Atomic and durable save: writes path + ".tmp" and flushes it to the disk, then renames it over
// path, so neither a failed save nor a power cut leaves a partial file at path. Throws a
// StorageError on failure, as serializeModifications.
void saveModifications(const std::filesystem::path& path, const ModificationStore& store,
                       std::uint64_t seed);

// Throws a StorageError if the file cannot be read or is invalid (see
// deserializeModifications).
[[nodiscard]] SavedModifications loadModifications(const std::filesystem::path& path);

} // namespace terrain

#pragma once

// The columns the tiles of one world share (internal to terrain_core: terrain_tiles.cpp). A tile
// samples its 17 x 17 columns, the points one step beyond its sides for the blend of its sides
// (TileColumns::outsideHeights) and, with rock, the ring of columns beyond them (sampleRing): its
// two outer rows on each side, its outside points and its ring are the outer rows of its
// neighbors of the same LOD, sampled again by them. The cache keeps those shared columns, so a
// tile takes from it what a neighbor sampled before.
//
// A sample holds the same bits in any batch (Heightfield::samplesAt), so a tile is the same, bit
// for bit, whatever its neighbors, the order of the tiles and the threads. The cache holds what
// the generator gives and nothing else: the edits of the player (blocks, volumes, plants) are
// applied to the density of the tiles afterwards (TerrainTiles::generate) and never reach the
// columns, so it never has to forget anything for them (column_cache_test). It belongs to one
// TerrainTiles, one world of one seed, and goes with it: nothing passes from one world to the
// next.
//
// By tile: the shared columns of a tile (its outer rows, kSharedColumns of them) form a block,
// in the shard of the hash of its key. Thread-safe: each of the kColumnShards shards its own lock;
// a batch of a tile locks the shards of the tiles it meets (itself and its 8 neighbors at most),
// each once, and copies there, nothing allocated under a lock. Bounded: a shard holds a fixed
// number of blocks, the oldest giving way to a new one, all allocated on its first use.

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/rock_faces.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace terrain::tile {

inline constexpr std::size_t kColumnShards = 32;
// The outer rows of a tile of 16 x 16 cells: its columns 0, 1 and 15 along x or along z.
inline constexpr std::size_t kSharedColumns = 16 * 16 - 13 * 13;

class ColumnCache {
public:
    // About capacity columns at most (whole blocks of kSharedColumns in kColumnShards shards, at
    // least one each; 0: none, every column sampled again); with rock, their rock too.
    ColumnCache(std::size_t capacity, bool rock);

    // The samples of the points (xs, zs) of a tile whose cells are spacing meters (its LOD), in
    // out, with their rock when rock is not empty (Heightfield::samplesAt with the rock of the
    // detail of the LOD: leastRun, the same for every tile of cells of spacing meters): those the
    // cache holds copied from it, the others sampled in one batch, and kept when shared with
    // another tile (sharedColumn). Only with rock when the cache was made with rock, and the
    // other way round; otherwise every point is sampled.
    void samplesAt(const Heightfield& field, int spacing, std::span<const double> xs,
                   std::span<const double> zs, std::span<HeightSample> out,
                   std::span<RockColumn> rock, float leastRun);

    [[nodiscard]] ColumnCacheStats stats() const;
    // The most memory its columns take, never more (all its blocks allocated).
    [[nodiscard]] std::size_t bytesBound() const;

    // A shared point: the tile holding it (in tiles of 16 cells, and the cells), its place in
    // the block of that tile, its index in the batch.
    struct Point {
        std::int64_t tileX = 0;
        std::int64_t tileZ = 0;
        std::int32_t spacing = 0;
        std::uint32_t slot = 0;
        std::size_t index = 0;
    };

private:
    struct Shard {
        std::mutex mutex;
        std::vector<Point> heads;          // the tile of each block (slot, index unused)
        std::vector<std::uint8_t> present; // blocks x kSharedColumns
        std::vector<HeightSample> samples; // the same
        std::vector<RockColumn> rocks;     // the same, with rock only
        std::size_t used = 0;              // blocks in use
        std::size_t next = 0;              // the oldest block, replaced next once all are used
        std::size_t held = 0;              // columns present
    };

    std::unique_lock<std::mutex> lock(Shard& shard);
    void allocate(Shard& shard) const;
    // The block of the tile of p in shard, or blocks_ without one (a new one with create).
    std::size_t blockOf(Shard& shard, const Point& p, bool create);
    void find(std::span<const Point> points, std::span<HeightSample> out,
              std::span<RockColumn> rock, std::vector<char>& found);
    void keep(std::span<const Point> points, std::span<const char> found,
              std::span<const HeightSample> out, std::span<const RockColumn> rock);
    [[nodiscard]] std::size_t bytesOf(const Shard& shard) const;

    std::size_t blocks_; // per shard
    bool rock_;
    std::unique_ptr<std::array<Shard, kColumnShards>> shards_;
    std::atomic<std::uint64_t> lookups_{0};
    std::atomic<std::uint64_t> hits_{0};
    std::atomic<std::uint64_t> locks_{0};
    std::atomic<std::uint64_t> contended_{0};
    std::atomic<std::uint64_t> waitNanoseconds_{0};
};

// Whether the lattice point (x, z) of the tiles of cells of spacing meters lies on the two outer
// rows of the side of a tile (its index along x or z 0, 1 or kChunkCells - 1 in its tile): the
// points the neighboring tiles sample too.
[[nodiscard]] bool sharedColumn(std::int64_t x, std::int64_t z, int spacing);

} // namespace terrain::tile

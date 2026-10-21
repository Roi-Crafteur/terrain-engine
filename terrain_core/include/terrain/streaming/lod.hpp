#pragma once

#include "terrain/chunk_coord.hpp"
#include "terrain/meshing/density_grid.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace terrain {

// Levels of detail of the streamed world. A tile of LOD L covers 2^L x 2^L chunks and is meshed
// in vertical levels of kChunkCells^3 cells of 2^L meters: 1 m at LOD 0 (full resolution), 2 m
// at LOD 1, 4 m at LOD 2 and 8 m at LOD 3, the simplified far terrain. Every tile costs the
// same, and each level covers 4 times the area of the previous one.
inline constexpr int kLodCount = 4;
inline constexpr int kMaxLod = kLodCount - 1;

// Chunks along each side of a tile, side of its cells and side of the tile, in meters.
[[nodiscard]] constexpr int tileChunks(int lod) {
    return 1 << lod;
}
[[nodiscard]] constexpr double tileCellMeters(int lod) {
    return kCellSizeMeters * tileChunks(lod);
}
[[nodiscard]] constexpr double tileSizeMeters(int lod) {
    return kChunkSizeMeters * tileChunks(lod);
}

// A tile: its first chunk (lowest x and z), a multiple of tileChunks(lod) on each axis.
struct TileKey {
    ChunkCoord origin;
    int lod = 0;

    friend bool operator==(const TileKey&, const TileKey&) = default;
};

struct TileKeyHash {
    [[nodiscard]] std::size_t operator()(const TileKey& key) const;
};

// The tile of LOD lod that contains the chunk.
[[nodiscard]] TileKey tileContaining(ChunkCoord chunk, int lod);
[[nodiscard]] bool tileContains(const TileKey& tile, ChunkCoord chunk);

// The tile with its origin brought back into the canonical range of the world (wrapChunk): the
// same key for every image of the tile. The sides of the world are multiples of two tiles of
// the coarsest LOD, so the canonical tiles never straddle the edge of the range.
[[nodiscard]] TileKey wrapTile(const TileKey& tile, const WorldSize& world);
static_assert(static_cast<long long>(kWorldAlignmentMeters) %
                      (2 * static_cast<long long>(tileSizeMeters(kMaxLod))) ==
                  0,
              "the world alignment must be a multiple of two tiles of the coarsest LOD");

// The image of the tile nearest to the chunk, its origin moved by whole worlds to lie within
// half a world of the chunk along each axis: the tile as a player in that chunk sees it.
[[nodiscard]] TileKey nearestTile(const TileKey& tile, ChunkCoord chunk, const WorldSize& world);

// Chebyshev distance between two chunks, in chunks.
[[nodiscard]] int chunkDistance(ChunkCoord a, ChunkCoord b);

// Distance from the chunk to the nearest chunk of the tile: 0 if the tile contains it.
[[nodiscard]] int tileDistance(const TileKey& tile, ChunkCoord chunk);

// Order of the tiles to load: nearest to the chunk first, then by LOD, z and x.
[[nodiscard]] bool loadsBefore(const TileKey& a, const TileKey& b, ChunkCoord player);

struct LodParams {
    // A chunk at distance d from the chunk of the player (chunkDistance) asks for LOD 0 if
    // d < splitDistance[0], LOD 1 if d < splitDistance[1], LOD 2 if d < splitDistance[2] and
    // LOD 3 beyond: full resolution within 64 m, 8 m cells beyond 256 m.
    std::array<int, kMaxLod> splitDistance = {4, 8, 16};
    // Every chunk up to this distance from the player is covered by a tile.
    int viewDistance = 32;
};

// Throws std::invalid_argument unless splitDistance[0] >= 1, viewDistance >= 0 and
// splitDistance[L] - splitDistance[L - 1] >= tileChunks(L) for L = 1, 2: the gaps that keep
// two touching tiles within one LOD of each other.
void validate(const LodParams& params);

// LOD asked for by a chunk at this distance from the player.
[[nodiscard]] int lodForDistance(int distance, const LodParams& params);

// Tiles around the player, as the leaves of quadtrees whose roots are the LOD 3 tiles: a tile
// of LOD L > 0 is split into its 4 children of LOD L - 1 when its nearest chunk is closer than
// splitDistance[L - 1]. Every chunk within viewDistance lies in exactly one tile, no tile is
// coarser than one of its chunks asks for, and two tiles touching by a face or a corner differ
// by at most one LOD (2:1 balance). Sorted with loadsBefore. Deterministic. Throws like
// validate.
[[nodiscard]] std::vector<TileKey> selectTiles(ChunkCoord player, const LodParams& params);

// Sides of a tile bordering a tile of the next coarser LOD, as bits of a mask: its 4 faces, and
// its 4 vertical corner columns touched by a coarser tile across the diagonal only.
enum TileSeam : unsigned {
    kSeamNegX = 1U << 0,
    kSeamPosX = 1U << 1,
    kSeamNegZ = 1U << 2,
    kSeamPosZ = 1U << 3,
    kSeamNegXNegZ = 1U << 4,
    kSeamPosXNegZ = 1U << 5,
    kSeamNegXPosZ = 1U << 6,
    kSeamPosXPosZ = 1U << 7,
};

// A tile to generate, with the sides where it must match a coarser neighbor.
struct TileRequest {
    TileKey key;
    unsigned seams = 0; // TileSeam bits

    friend bool operator==(const TileRequest&, const TileRequest&) = default;
};

// Requests of a selection (in its order), with the seams of each tile against the others.
[[nodiscard]] std::vector<TileRequest> tileRequests(const std::vector<TileKey>& selection);

// Seams between LODs. The samples of a coarser neighbor are the even samples of the grid on the
// shared side (tiles are aligned on their size). On each face of the mask, every sample at an
// odd index takes the bilinear interpolation of the even samples around it; on each corner
// column, every odd sample takes the mean of the two even samples above and below. The side
// then holds the density the coarser tile interpolates there, so the two meshes meet: for a
// density linear along y such as y - height(x, z), every vertex of the fine side lies on the
// coarse side (no crack, only T-junctions). Even samples are never changed.
void stitchSeams(DensityGrid& grid, unsigned seams);

} // namespace terrain

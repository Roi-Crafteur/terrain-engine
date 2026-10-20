#include "terrain/streaming/lod.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <tuple>
#include <unordered_set>

namespace terrain {

namespace {

using TileSet = std::unordered_set<TileKey, TileKeyHash>;

// Floor division: chunks are aligned on tile sizes on the negative side too.
int floorDiv(int value, int divisor) {
    const int quotient = value / divisor;
    return quotient * divisor > value ? quotient - 1 : quotient;
}

// Gap between a chunk and the interval [first, first + size), in chunks.
int axisGap(int chunk, int first, int size) {
    return std::max({first - chunk, 0, chunk - (first + size - 1)});
}

void addLeaves(const TileKey& tile, ChunkCoord player, const LodParams& params,
               std::vector<TileKey>& tiles) {
    const int distance = tileDistance(tile, player);
    if (distance > params.viewDistance) {
        return;
    }
    const auto lod = static_cast<std::size_t>(tile.lod);
    if (tile.lod == 0 || distance >= params.splitDistance[lod - 1]) {
        tiles.push_back(tile);
        return;
    }
    const int half = tileChunks(tile.lod - 1);
    for (const int dz : {0, half}) {
        for (const int dx : {0, half}) {
            addLeaves({{tile.origin.x + dx, tile.origin.z + dz}, tile.lod - 1}, player, params,
                      tiles);
        }
    }
}

// True if the chunk lies in a selected tile of the next coarser LOD than tile.
bool coarserAt(const TileKey& tile, ChunkCoord chunk, const TileSet& selection) {
    return tile.lod < kMaxLod && selection.contains(tileContaining(chunk, tile.lod + 1));
}

unsigned seamsOf(const TileKey& tile, const TileSet& selection) {
    const int size = tileChunks(tile.lod);
    const int x0 = tile.origin.x;
    const int z0 = tile.origin.z;
    const struct {
        ChunkCoord outside;
        TileSeam seam;
    } sides[] = {
        {{x0 - 1, z0}, kSeamNegX},
        {{x0 + size, z0}, kSeamPosX},
        {{x0, z0 - 1}, kSeamNegZ},
        {{x0, z0 + size}, kSeamPosZ},
        {{x0 - 1, z0 - 1}, kSeamNegXNegZ},
        {{x0 + size, z0 - 1}, kSeamPosXNegZ},
        {{x0 - 1, z0 + size}, kSeamNegXPosZ},
        {{x0 + size, z0 + size}, kSeamPosXPosZ},
    };
    unsigned seams = 0;
    for (const auto& side : sides) {
        if (coarserAt(tile, side.outside, selection)) {
            seams |= side.seam;
        }
    }
    return seams;
}

struct GridPoint {
    int x = 0;
    int y = 0;
    int z = 0;
};

// A vertical face of the grid: a is the sample index along y, b along the horizontal axis.
struct Face {
    bool alongZ = false; // b runs along z (the face is at a fixed x), else along x
    int fixed = 0;       // index of the face on the other horizontal axis

    [[nodiscard]] GridPoint at(int a, int b) const {
        return alongZ ? GridPoint{fixed, a, b} : GridPoint{b, a, fixed};
    }
};

float read(const DensityGrid& grid, GridPoint p) {
    return grid.at(p.x, p.y, p.z);
}

void stitchFace(DensityGrid& grid, Face face) {
    for (int b = 0; b < kGridSamples; ++b) {
        for (int a = 0; a < kGridSamples; ++a) {
            const bool oddA = a % 2 != 0;
            const bool oddB = b % 2 != 0;
            float value = 0.0f;
            if (oddA && oddB) {
                value = (read(grid, face.at(a - 1, b - 1)) + read(grid, face.at(a + 1, b - 1)) +
                         read(grid, face.at(a - 1, b + 1)) + read(grid, face.at(a + 1, b + 1))) /
                        4.0f;
            } else if (oddA) {
                value = (read(grid, face.at(a - 1, b)) + read(grid, face.at(a + 1, b))) / 2.0f;
            } else if (oddB) {
                value = (read(grid, face.at(a, b - 1)) + read(grid, face.at(a, b + 1))) / 2.0f;
            } else {
                continue;
            }
            const GridPoint p = face.at(a, b);
            grid.set(p.x, p.y, p.z, value);
        }
    }
}

void stitchColumn(DensityGrid& grid, int x, int z) {
    for (int y = 1; y < kGridSamples; y += 2) {
        grid.set(x, y, z, (grid.at(x, y - 1, z) + grid.at(x, y + 1, z)) / 2.0f);
    }
}

} // namespace

std::size_t TileKeyHash::operator()(const TileKey& key) const {
    const auto x = static_cast<std::uint32_t>(key.origin.x);
    const auto z = static_cast<std::uint32_t>(key.origin.z);
    std::uint64_t h = (static_cast<std::uint64_t>(x) << 32) | z;
    h ^= static_cast<std::uint64_t>(key.lod) * 0x9E3779B97F4A7C15ULL;
    h ^= h >> 29;
    h *= 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 32;
    return static_cast<std::size_t>(h);
}

TileKey tileContaining(ChunkCoord chunk, int lod) {
    const int size = tileChunks(lod);
    return {{floorDiv(chunk.x, size) * size, floorDiv(chunk.z, size) * size}, lod};
}

TileKey wrapTile(const TileKey& tile, const WorldSize& world) {
    return {wrapChunk(tile.origin, world), tile.lod};
}

TileKey nearestTile(const TileKey& tile, ChunkCoord chunk, const WorldSize& world) {
    const ChunkCoord count = worldChunks(world);
    const auto nearest = [](std::int64_t origin, std::int64_t center, std::int64_t period) {
        const std::int64_t offset = wrapIndex(origin - center, period);
        return static_cast<int>(center + offset);
    };
    return {{nearest(tile.origin.x, chunk.x, count.x), nearest(tile.origin.z, chunk.z, count.z)},
            tile.lod};
}

bool tileContains(const TileKey& tile, ChunkCoord chunk) {
    return tileDistance(tile, chunk) == 0;
}

int chunkDistance(ChunkCoord a, ChunkCoord b) {
    return std::max(std::abs(a.x - b.x), std::abs(a.z - b.z));
}

int tileDistance(const TileKey& tile, ChunkCoord chunk) {
    const int size = tileChunks(tile.lod);
    return std::max(axisGap(chunk.x, tile.origin.x, size), axisGap(chunk.z, tile.origin.z, size));
}

bool loadsBefore(const TileKey& a, const TileKey& b, ChunkCoord player) {
    return std::tuple(tileDistance(a, player), a.lod, a.origin.z, a.origin.x) <
           std::tuple(tileDistance(b, player), b.lod, b.origin.z, b.origin.x);
}

void validate(const LodParams& params) {
    if (params.splitDistance[0] < 1 || params.viewDistance < 0) {
        throw std::invalid_argument("LodParams: invalid split or view distance");
    }
    for (int lod = 1; lod < kMaxLod; ++lod) {
        const auto i = static_cast<std::size_t>(lod);
        if (params.splitDistance[i] - params.splitDistance[i - 1] < tileChunks(lod)) {
            throw std::invalid_argument("LodParams: split distances too close for a 2:1 balance");
        }
    }
}

int lodForDistance(int distance, const LodParams& params) {
    for (int lod = 0; lod < kMaxLod; ++lod) {
        if (distance < params.splitDistance[static_cast<std::size_t>(lod)]) {
            return lod;
        }
    }
    return kMaxLod;
}

std::vector<TileKey> selectTiles(ChunkCoord player, const LodParams& params) {
    validate(params);
    const int root = tileChunks(kMaxLod);
    const int reach = params.viewDistance;
    std::vector<TileKey> tiles;
    for (int z = floorDiv(player.z - reach, root) * root; z <= player.z + reach; z += root) {
        for (int x = floorDiv(player.x - reach, root) * root; x <= player.x + reach; x += root) {
            addLeaves({{x, z}, kMaxLod}, player, params, tiles);
        }
    }
    std::sort(tiles.begin(), tiles.end(),
              [player](const TileKey& a, const TileKey& b) { return loadsBefore(a, b, player); });
    return tiles;
}

std::vector<TileRequest> tileRequests(const std::vector<TileKey>& selection) {
    const TileSet lookup(selection.begin(), selection.end());
    std::vector<TileRequest> requests;
    requests.reserve(selection.size());
    for (const TileKey& tile : selection) {
        requests.push_back({tile, seamsOf(tile, lookup)});
    }
    return requests;
}

void stitchSeams(DensityGrid& grid, unsigned seams) {
    constexpr int kLast = kGridSamples - 1;
    if ((seams & kSeamNegX) != 0) {
        stitchFace(grid, {true, 0});
    }
    if ((seams & kSeamPosX) != 0) {
        stitchFace(grid, {true, kLast});
    }
    if ((seams & kSeamNegZ) != 0) {
        stitchFace(grid, {false, 0});
    }
    if ((seams & kSeamPosZ) != 0) {
        stitchFace(grid, {false, kLast});
    }
    // A corner column on a stitched face is already stitched the same way: no harm in redoing it.
    if ((seams & kSeamNegXNegZ) != 0) {
        stitchColumn(grid, 0, 0);
    }
    if ((seams & kSeamPosXNegZ) != 0) {
        stitchColumn(grid, kLast, 0);
    }
    if ((seams & kSeamNegXPosZ) != 0) {
        stitchColumn(grid, 0, kLast);
    }
    if ((seams & kSeamPosXPosZ) != 0) {
        stitchColumn(grid, kLast, kLast);
    }
}

} // namespace terrain

#pragma once

#include "terrain/world_size.hpp"

#include <cstdint>

namespace terrain {

// Horizontal size of a chunk along X and Z, in meters.
inline constexpr double kChunkSizeMeters = 16.0;

// Integer coordinate of a chunk in the horizontal (X, Z) plane.
struct ChunkCoord {
    std::int32_t x = 0;
    std::int32_t z = 0;

    friend bool operator==(const ChunkCoord&, const ChunkCoord&) = default;
};

// Returns the chunk containing the world position (x, z), in meters.
// Uses floor division: chunk n covers [n * 16, (n + 1) * 16), negatives included.
[[nodiscard]] ChunkCoord worldToChunk(double x, double z);

// Chunks along each side of the world: its sides divided by kChunkSizeMeters.
[[nodiscard]] ChunkCoord worldChunks(const WorldSize& world);

// The chunk brought back into the canonical range of the world (WorldSize), on each axis
// [-n / 2, n / 2) for n chunks along it: the same chunk for every image.
[[nodiscard]] ChunkCoord wrapChunk(ChunkCoord chunk, const WorldSize& world);

} // namespace terrain

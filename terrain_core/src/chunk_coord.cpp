#include "terrain/chunk_coord.hpp"

#include <cmath>

namespace terrain {

namespace {

std::int32_t toChunkIndex(double meters) {
    return static_cast<std::int32_t>(std::floor(meters / kChunkSizeMeters));
}

} // namespace

ChunkCoord worldToChunk(double x, double z) {
    return {toChunkIndex(x), toChunkIndex(z)};
}

ChunkCoord worldChunks(const WorldSize& world) {
    return {static_cast<std::int32_t>(world.x / kChunkSizeMeters),
            static_cast<std::int32_t>(world.z / kChunkSizeMeters)};
}

ChunkCoord wrapChunk(ChunkCoord chunk, const WorldSize& world) {
    const ChunkCoord count = worldChunks(world);
    return {static_cast<std::int32_t>(wrapIndex(chunk.x, count.x)),
            static_cast<std::int32_t>(wrapIndex(chunk.z, count.z))};
}

} // namespace terrain

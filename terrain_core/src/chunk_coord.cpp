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

} // namespace terrain

#include "terrain/chunk_coord.hpp"

#include "test_assert.hpp"

#include <initializer_list>

namespace {

using terrain::ChunkCoord;
using terrain::worldToChunk;

void testOrigin() {
    TEST_CHECK(worldToChunk(0.0, 0.0) == ChunkCoord{0, 0});
    TEST_CHECK(worldToChunk(-0.0, -0.0) == ChunkCoord{0, 0});
}

void testPositive() {
    TEST_CHECK(worldToChunk(5.5, 31.9) == ChunkCoord{0, 1});
    TEST_CHECK(worldToChunk(100.0, 1000.0) == ChunkCoord{6, 62});
}

void testNegative() {
    TEST_CHECK(worldToChunk(-0.001, -5.0) == ChunkCoord{-1, -1});
    TEST_CHECK(worldToChunk(-17.0, -100.0) == ChunkCoord{-2, -7});
}

void testExactBoundaries() {
    TEST_CHECK(worldToChunk(16.0, 32.0) == ChunkCoord{1, 2});
    TEST_CHECK(worldToChunk(-16.0, -32.0) == ChunkCoord{-1, -2});
    TEST_CHECK(worldToChunk(15.999, -16.001) == ChunkCoord{0, -2});
}

void testWrapChunk() {
    const terrain::WorldSize world;
    const ChunkCoord count = terrain::worldChunks(world);
    TEST_CHECK(count == ChunkCoord{3751936, 2187264});
    TEST_CHECK(terrain::wrapChunk({0, 0}, world) == ChunkCoord{0, 0});
    TEST_CHECK(terrain::wrapChunk({-5, 7}, world) == ChunkCoord{-5, 7});
    TEST_CHECK(terrain::wrapChunk({count.x / 2, count.z / 2}, world) ==
               ChunkCoord{-count.x / 2, -count.z / 2});
    TEST_CHECK(terrain::wrapChunk({-count.x / 2 - 1, count.z / 2 - 1}, world) ==
               ChunkCoord{count.x / 2 - 1, count.z / 2 - 1});
    for (int i = -40; i <= 40; ++i) {
        const ChunkCoord chunk{i * 93761 + 5, i * 54721 - 3};
        const ChunkCoord wrapped = terrain::wrapChunk(chunk, world);
        TEST_CHECK(wrapped.x >= -count.x / 2 && wrapped.x < count.x / 2);
        TEST_CHECK(wrapped.z >= -count.z / 2 && wrapped.z < count.z / 2);
        TEST_CHECK((chunk.x - wrapped.x) % count.x == 0 && (chunk.z - wrapped.z) % count.z == 0);
        TEST_CHECK(terrain::wrapChunk({chunk.x - count.x, chunk.z + count.z}, world) == wrapped);
    }
    // The chunk of a wrapped position is the wrapped chunk of the position.
    for (const double x : {-0.5 * world.x, -0.5 * world.x - 0.25, 0.5 * world.x - 1.0, 1234.5}) {
        const ChunkCoord direct = worldToChunk(terrain::wrapCoordinate(x, world.x), 0.0);
        TEST_CHECK(terrain::wrapChunk(worldToChunk(x, 0.0), world) == direct);
    }
}

} // namespace

int main() {
    testOrigin();
    testPositive();
    testNegative();
    testExactBoundaries();
    testWrapChunk();
    return test::result();
}

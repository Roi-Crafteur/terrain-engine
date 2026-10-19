#include "terrain/chunk_coord.hpp"

#include "test_assert.hpp"

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

} // namespace

int main() {
    testOrigin();
    testPositive();
    testNegative();
    testExactBoundaries();
    return test::result();
}

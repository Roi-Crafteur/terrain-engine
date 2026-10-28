// Golden checksums of tiles edited block by block, pinned with the block storage of phase 7
// (one entry per block) before the storage by sections of phase 15: the sections, their
// density and their levels must give the same tiles bit for bit. The edits dig and build
// around a corner of 4 chunks, across a level boundary and on the edge x = world.x / 2 of the
// wrapped world; the tiles cover LOD 0 to 3, with and without stitched seams, with and without
// caves. The plants are left out: phase 15 drops the generated plants over removed ground. The
// parts holding a tile stitched over the edits ("lod 0 stitched", "lod 1", "lod 2", "caves lod
// 0") were pinned again when the stitched sides followed the coarse outline (lod_seams.cpp):
// their vertices moved on the seams only, every other part kept its value.
#include "terrain/storage/modifications.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "golden_support.hpp"
#include "legacy_relief_support.hpp"
#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using terrain::BlockEdit;
using terrain::ChunkCoord;
using terrain::TerrainTiles;
using terrain::TileData;
using terrain::TileRequest;

constexpr unsigned kAllSeams = 0xFFU;

// A tile without its plants.
void addTileWithoutPlants(golden::Checksum& sum, const TileData& data) {
    sum.add(data.originX);
    sum.add(data.originY);
    sum.add(data.originZ);
    sum.add(data.minHeight);
    sum.add(data.maxHeight);
    golden::addMesh(sum, data.mesh);
    golden::addMaterials(sum, data.materials);
    golden::addMesh(sum, data.water);
    golden::addMaterials(sum, data.waterMaterials);
    sum.add(static_cast<std::uint64_t>(data.speleothems.size()));
    for (const terrain::Speleothem& s : data.speleothems) {
        golden::addSpeleothem(sum, s);
    }
    golden::addMesh(sum, data.lava);
}

int groundAt(const TerrainTiles& tiles, int x, int z) {
    return static_cast<int>(std::floor(tiles.heightfield().heightAt(x, z)));
}

// Edits around the corner (x, z) of 4 chunks: a pit across the corner, deep enough to reach a
// sample of every LOD, a pillar, a block built against the pit, a column across the level
// boundary under the ground, a lone block in the air.
void editCorner(TerrainTiles& tiles, int x, int z) {
    const int ground = groundAt(tiles, x, z);
    for (int dz = -2; dz <= 1; ++dz) {
        for (int dx = -2; dx <= 1; ++dx) {
            for (int y = ground - 5; y <= ground + 3; ++y) {
                tiles.setBlock({x + dx, y, z + dz}, BlockEdit::Removed);
            }
        }
    }
    tiles.setBlock({x + 2, ground, z}, BlockEdit::Added);
    const int pillar = groundAt(tiles, x + 5, z + 5);
    for (int y = pillar + 1; y <= pillar + 4; ++y) {
        tiles.setBlock({x + 5, y, z + 5}, BlockEdit::Added);
    }
    const int boundary = 16 * static_cast<int>(std::floor((ground - 20) / 16.0));
    for (int y = boundary - 1; y <= boundary + 1; ++y) {
        tiles.setBlock({x - 7, y, z + 6}, BlockEdit::Removed);
    }
    tiles.setBlock({x + 6, ground + 9, z - 6}, BlockEdit::Added);
}

std::uint64_t tilesChecksum(const TerrainTiles& tiles, const std::vector<TileRequest>& requests) {
    golden::Checksum sum;
    for (const TileRequest& request : requests) {
        addTileWithoutPlants(sum, tiles.generate(request));
    }
    return sum.value();
}

// LOD 0 tiles of the 4 chunks around the corner of chunk c, then the tiles of each LOD over it.
std::vector<golden::Part> cornerParts(const TerrainTiles& tiles, ChunkCoord c) {
    std::vector<TileRequest> lod0;
    std::vector<TileRequest> stitched;
    for (int dz = -1; dz <= 0; ++dz) {
        for (int dx = -1; dx <= 0; ++dx) {
            lod0.push_back({{{c.x + dx, c.z + dz}, 0}, 0});
            stitched.push_back({{{c.x + dx, c.z + dz}, 0}, kAllSeams});
        }
    }
    std::vector<golden::Part> parts;
    parts.push_back({"lod 0", tilesChecksum(tiles, lod0), 0});
    parts.push_back({"lod 0 stitched", tilesChecksum(tiles, stitched), 0});
    for (int lod = 1; lod <= terrain::kMaxLod; ++lod) {
        const std::vector<TileRequest> coarse = {
            {terrain::tileContaining(c, lod), 0},
            {terrain::tileContaining({c.x - 1, c.z - 1}, lod), kAllSeams}};
        parts.push_back({lod == 1 ? "lod 1" : (lod == 2 ? "lod 2" : "lod 3"),
                         tilesChecksum(tiles, coarse), 0});
    }
    return parts;
}

void setExpected(std::vector<golden::Part>& parts, const std::vector<std::uint64_t>& expected) {
    for (std::size_t i = 0; i < parts.size() && i < expected.size(); ++i) {
        parts[i].expected = expected[i];
    }
}

// Every part changes with the edits: the checksums pin edited tiles.
void checkEdited(const std::vector<golden::Part>& edited,
                 const std::vector<golden::Part>& unedited) {
    for (std::size_t i = 0; i < edited.size() && i < unedited.size(); ++i) {
        TEST_CHECK(edited[i].actual != unedited[i].actual);
    }
}

void testWithoutCaves() {
    TerrainTiles tiles(streaming_test::kSeed);
    const ChunkCoord c{streaming_test::kCenterChunkX, streaming_test::kCenterChunkZ};
    const std::vector<golden::Part> unedited = cornerParts(tiles, c);
    editCorner(tiles, c.x * 16, c.z * 16);
    std::vector<golden::Part> parts = cornerParts(tiles, c);
    checkEdited(parts, unedited);
    setExpected(parts, {0xFA759A4827F8081AULL, 0x9DB6444E4434856EULL, 0x502A0E5C2E991D56ULL,
                        0xD0707F45E9888BF0ULL, 0xC3F45C7C65977905ULL});
    golden::report("tiles edited block by block, without caves", parts);
}

void testWithCaves() {
    TerrainTiles tiles(streaming_test::kSeed, test_relief::legacyRelief(), {},
                       terrain::CaveParams{});
    const ChunkCoord c{streaming_test::kCenterChunkX, streaming_test::kCenterChunkZ};
    const std::vector<TileRequest> lod0 = {{{c, 0}, 0}, {{{c.x - 1, c.z - 1}, 0}, kAllSeams}};
    const std::vector<TileRequest> lod1 = {{terrain::tileContaining(c, 1), 0}};
    const std::vector<golden::Part> unedited = {{"caves lod 0", tilesChecksum(tiles, lod0), 0},
                                                {"caves lod 1", tilesChecksum(tiles, lod1), 0}};
    editCorner(tiles, c.x * 16, c.z * 16);
    std::vector<golden::Part> parts = {{"caves lod 0", tilesChecksum(tiles, lod0), 0},
                                       {"caves lod 1", tilesChecksum(tiles, lod1), 0}};
    checkEdited(parts, unedited);
    setExpected(parts, {0x88816AE634FAF0F8ULL, 0x9FF94D3DD008D9B2ULL});
    golden::report("tiles edited block by block, with caves", parts);
}

// Edits on the edge x = world.x / 2, given at both images, read by the tiles on both sides.
void testAcrossTheEdge() {
    TerrainTiles tiles(streaming_test::kSeed);
    const ChunkCoord count = terrain::worldChunks(terrain::WorldSize{});
    const int east = count.x / 2 - 1;
    const int west = -count.x / 2;
    constexpr int kRow = -641774;
    const std::vector<TileRequest> requests = {{{{east, kRow}, 0}, 0},
                                               {{{west, kRow}, 0}, 0},
                                               {{{east, kRow - 1}, 0}, kAllSeams},
                                               {{{west + count.x, kRow}, 0}, 0},
                                               {terrain::tileContaining({west, kRow}, 1), 0}};
    const std::vector<golden::Part> unedited = {{"wrap", tilesChecksum(tiles, requests), 0}};
    editCorner(tiles, count.x * 8, kRow * 16 + 8);
    std::vector<golden::Part> parts = {{"wrap", tilesChecksum(tiles, requests), 0}};
    checkEdited(parts, unedited);
    setExpected(parts, {0x014BA5DB0B1E42A8ULL});
    golden::report("tiles edited block by block, across the edge of the world", parts);
}

} // namespace

int main() {
    testWithoutCaves();
    testWithCaves();
    testAcrossTheEdge();
    return test::result();
}

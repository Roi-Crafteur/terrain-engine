// The surface blend of the streamed tiles (TileData::surfaceBlend): kTileBlendTexels x
// kTileBlendTexels texels on every LOD, each the blend of its column on the site of the lattice of
// the tile (its column and its 4 neighbors one cell away), whose dominant biome and material are
// those of the ecology on that site; the same bits on the sides two tiles of a LOD share, across
// the edges of the world too; unchanged by the seams, the caves and the edits (the natural
// ground); the same on worker threads; texels every 8 cells are those of every 4 at their columns.

#include "tile_blend.hpp"

#include "terrain/generation/biome_blend.hpp"
#include "terrain/generation/terrain_surface.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

namespace {

using terrain::BlendTexel;
using terrain::ChunkCoord;
using terrain::kTileBlendTexels;
using terrain::TerrainTiles;
using terrain::TileData;
using terrain::TileKey;

const TerrainTiles& tiles() {
    static const TerrainTiles t(streaming_test::kSeed);
    return t;
}

BlendTexel texelAt(const TileData& data, int i, int k) {
    return data.surfaceBlend[static_cast<std::size_t>(k * kTileBlendTexels + i)];
}

double pitchOf(int lod) {
    return terrain::kTileBlendStride * terrain::tileCellMeters(lod);
}

// Each texel against its column computed point by point.
void testTexelsOfTheirColumns(ChunkCoord chunk) {
    const terrain::Heightfield& field = tiles().heightfield();
    const terrain::Ecology& ecology = tiles().ecology();
    bool texels = true;
    bool dominants = true;
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        const TileData data = tiles().generate({terrain::tileContaining(chunk, lod), 0});
        TEST_CHECK(data.surfaceBlend.size() ==
                   static_cast<std::size_t>(kTileBlendTexels * kTileBlendTexels));
        const double cell = terrain::tileCellMeters(lod);
        for (int k = 0; k < kTileBlendTexels; ++k) {
            for (int i = 0; i < kTileBlendTexels; ++i) {
                const double x = data.originX + i * pitchOf(lod);
                const double z = data.originZ + k * pitchOf(lod);
                const terrain::HeightSample s = field.sampleAt(x, z);
                const terrain::SurfaceSite site = terrain::heightfieldSite(
                    s.height, s.continentalness,
                    {field.heightAt(x + cell, z), field.heightAt(x - cell, z),
                     field.heightAt(x, z + cell), field.heightAt(x, z - cell)},
                    cell);
                const terrain::ClimateSample climate = ecology.climate().sample(x, z, site);
                texels = texels && texelAt(data, i, k) ==
                                       terrain::packBlend(ecology.blendAt(x, z, site, climate));
                const terrain::EcologySample e = ecology.sampleAt(x, z, site);
                const terrain::SurfaceBlend b = terrain::unpackBlend(texelAt(data, i, k));
                dominants = dominants && b.biomes[0] == e.biome && b.materials[0] == e.material;
            }
        }
    }
    TEST_CHECK(texels);
    TEST_CHECK(dominants);
}

// The east side of a tile and the west side of the next, the south side and the north side.
bool sharedSides(const TileKey& key) {
    const int step = terrain::tileChunks(key.lod);
    const TileData here = tiles().generate({key, 0});
    const TileData east = tiles().generate({{{key.origin.x + step, key.origin.z}, key.lod}, 0});
    const TileData south = tiles().generate({{{key.origin.x, key.origin.z + step}, key.lod}, 0});
    const int last = kTileBlendTexels - 1;
    bool same = true;
    for (int t = 0; t < kTileBlendTexels; ++t) {
        same = same && texelAt(here, last, t) == texelAt(east, 0, t);
        same = same && texelAt(here, t, last) == texelAt(south, t, 0);
    }
    return same;
}

void testSharedSides(ChunkCoord chunk) {
    const ChunkCoord count = terrain::worldChunks(tiles().world());
    const ChunkCoord corner{count.x / 2 - 1, count.z / 2 - 1};
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        TEST_CHECK(sharedSides(terrain::tileContaining(chunk, lod)));
        TEST_CHECK(sharedSides(terrain::tileContaining(corner, lod)));
    }
}

void testNaturalGround(ChunkCoord chunk) {
    const TileKey key{chunk, 0};
    const std::vector<BlendTexel> plain = tiles().generate({key, 0}).surfaceBlend;
    TEST_CHECK(tiles().generate({key, terrain::kSeamPosX | terrain::kSeamNegXNegZ}).surfaceBlend ==
               plain);
    const TerrainTiles caves(streaming_test::kSeed, {}, {}, terrain::CaveParams{});
    TEST_CHECK(caves.generate({key, 0}).surfaceBlend == plain);
    TerrainTiles edited(streaming_test::kSeed);
    const auto x = static_cast<std::int32_t>(chunk.x * terrain::kChunkCells + 8);
    const auto z = static_cast<std::int32_t>(chunk.z * terrain::kChunkCells + 8);
    const auto y = static_cast<std::int32_t>(tiles().heightfield().heightAt(x, z));
    (void)edited.setBlock({x, y, z}, terrain::BlockEdit::Removed);
    (void)edited.setBlock({x, y + 3, z}, terrain::BlockEdit::Added);
    TEST_CHECK(edited.generate({key, 0}).surfaceBlend == plain);
}

void testThreads(ChunkCoord chunk) {
    std::vector<TileKey> keys;
    for (int k = 0; k < 4; ++k) {
        for (int i = 0; i < 4; ++i) {
            keys.push_back({{chunk.x + i, chunk.z + k}, (i + k) % terrain::kLodCount});
        }
    }
    std::vector<TileData> serial;
    for (const TileKey& key : keys) {
        serial.push_back(tiles().generate({key, 0}));
    }
    std::vector<TileData> parallel(keys.size());
    std::vector<std::thread> threads;
    for (std::size_t t = 0; t < 4; ++t) {
        threads.emplace_back([&, t] {
            for (std::size_t n = t; n < keys.size(); n += 4) {
                parallel[n] = tiles().generate({keys[n], 0});
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    TEST_CHECK(parallel == serial);
}

// Texels every 8 cells (3 x 3) are the texels every 4 at the same columns.
void testCoarserTexels(ChunkCoord chunk) {
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        const TileKey key = terrain::tileContaining(chunk, lod);
        const terrain::tile::TileLattice lattice = terrain::tile::latticeOf(key);
        const terrain::tile::TileColumns columns =
            terrain::tile::sampleColumns(tiles().heightfield(), lattice);
        const std::vector<BlendTexel> fine = terrain::tile::blendTexels(
            tiles().heightfield(), tiles().ecology(), lattice, columns, 4);
        const std::vector<BlendTexel> coarse = terrain::tile::blendTexels(
            tiles().heightfield(), tiles().ecology(), lattice, columns, 8);
        TEST_CHECK(fine == tiles().generate({key, 0}).surfaceBlend);
        TEST_CHECK(coarse.size() == 9);
        bool same = coarse.size() == 9;
        for (int k = 0; same && k < 3; ++k) {
            for (int i = 0; i < 3; ++i) {
                same = same && coarse[static_cast<std::size_t>(k * 3 + i)] ==
                                   fine[static_cast<std::size_t>(2 * k * kTileBlendTexels + 2 * i)];
            }
        }
        TEST_CHECK(same);
    }
}

} // namespace

int main() {
    const ChunkCoord mountain = streaming_test::mountainChunk(tiles().heightfield());
    const ChunkCoord plants = streaming_test::plantChunk(tiles(), 10);
    for (const ChunkCoord chunk : {mountain, plants}) {
        testTexelsOfTheirColumns(chunk);
        testSharedSides(chunk);
    }
    testNaturalGround(mountain);
    testThreads(plants);
    testCoarserTexels(mountain);
    return test::result();
}

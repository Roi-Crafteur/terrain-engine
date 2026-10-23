// Streamed tiles of the default world (the planetary geography), at a coast, a mountain and a
// plain found by search and at the corner of the world: every LOD meshes its ground, the same
// request gives the same tile, LOD 0 follows the Heightfield and the levels cover the ground;
// near the coast and on the plain the LOD 0 tiles hold plants, standing exactly on the
// Heightfield, the land plants above the sea.
#include "terrain/streaming/terrain_tiles.hpp"

#include "default_world_support.hpp"
#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <optional>

namespace {

using default_world::Place;
using terrain::ChunkCoord;
using terrain::TerrainTiles;
using terrain::TileData;

ChunkCoord chunkOf(const Place& p) {
    return terrain::worldToChunk(p.x, p.z);
}

// Tiles of every LOD over the chunk: meshed, deterministic, covering their ground; at LOD 0 on
// the Heightfield (within the interpolation along 1 m cells of a steep slope, 2 m on the stitched
// seams, across the sharp crests of the rock of the mountains: their slope breaks by about 1.5
// within a meter).
void testTiles(const TerrainTiles& tiles, ChunkCoord chunk) {
    bool meshed = true;
    bool same = true;
    bool covered = true;
    bool onSurface = true;
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        const terrain::TileRequest request{terrain::tileContaining(chunk, lod),
                                           terrain::kSeamNegX | terrain::kSeamPosZ};
        const TileData data = tiles.generate(request);
        meshed = meshed && !data.mesh.indices.empty() &&
                 data.mesh.normals.size() == data.mesh.vertices.size();
        same = same && data == tiles.generate(request);
        float low = data.maxHeight;
        float high = data.minHeight;
        for (std::size_t i = 0; i < data.mesh.vertices.size(); ++i) {
            const streaming_test::WorldPoint p = streaming_test::worldVertex(data, i);
            low = std::min(low, static_cast<float>(p.y));
            high = std::max(high, static_cast<float>(p.y));
            onSurface = onSurface && std::isfinite(p.y) &&
                        (lod > 0 || std::abs(p.y - tiles.heightfield().heightAt(p.x, p.z)) <= 0.5);
        }
        // The heights of the tile cover its vertices: along the seams the vertices follow the
        // coarser neighbor, so the extreme columns may lie between them.
        covered = covered && low >= data.minHeight - 1e-3f && high <= data.maxHeight + 1e-3f;
    }
    TEST_CHECK(meshed);
    TEST_CHECK(same);
    TEST_CHECK(covered);
    TEST_CHECK(onSurface);
}

// The plants of the LOD 0 tiles of a lattice of 512 m around a place (out to 10 km), until
// enough are found: on the Heightfield, the land plants above the sea.
std::size_t testPlants(const TerrainTiles& tiles, const Place& place) {
    std::size_t count = 0;
    bool standing = true;
    default_world::nearest(512.0, 20, [&](double dx, double dz) {
        const TileData data = tiles.generate({{chunkOf({place.x + dx, place.z + dz}), 0}, 0});
        for (const terrain::Plant& plant : data.plants) {
            standing = standing && plant.y == tiles.heightfield().heightAt(plant.x, plant.z) &&
                       (plant.type == terrain::PlantType::Seaweed || plant.y >= terrain::kSeaLevel);
        }
        count += data.plants.size();
        return count >= 50;
    });
    TEST_CHECK(standing);
    return count;
}

} // namespace

int main() {
    const std::optional<Place> coast = default_world::coast();
    const std::optional<Place> mountain = default_world::mountain();
    const std::optional<Place> plain = default_world::plain();
    TEST_CHECK(coast && mountain && plain);
    if (!coast || !mountain || !plain) {
        return test::result();
    }
    const TerrainTiles tiles(default_world::kSeed);
    const ChunkCoord count = terrain::worldChunks(tiles.world());
    for (const ChunkCoord chunk : {chunkOf(*coast), chunkOf(*mountain), chunkOf(*plain),
                                   ChunkCoord{count.x / 2 - 1, count.z / 2 - 1}}) {
        testTiles(tiles, chunk);
    }
    const std::size_t coastPlants = testPlants(tiles, *coast);
    const std::size_t plainPlants = testPlants(tiles, *plain);
    std::printf("default world: plants %zu near the coast, %zu on the plain\n", coastPlants,
                plainPlants);
    TEST_CHECK(coastPlants >= 50 && plainPlants >= 50);
    return test::result();
}

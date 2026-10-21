// surfaceFromHeightfield and sampleHeightGrid read the relief through the batch functions of the
// Heightfield, reusing the height of an adjacent site for a neighbor at the same coordinates bit
// for bit: their results must be those of the frozen implementation of commit 052078b, which
// reads it point by point (tests/frozen/frozen_surface.cpp), bit for bit. Regions: the map of
// the plants of LOD 0 tiles (a mountain chunk, a plant chunk, the last chunk at the corner of the
// world), larger maps, maps across the corner, one-pixel maps (the cave materials), a spacing
// whose neighbors never fall on the adjacent sites, and the planet.
#include "frozen/frozen_surface.hpp"

#include "terrain/chunk_coord.hpp"
#include "terrain/generation/height_grid.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/terrain_surface.hpp"

#include "bit_exact_support.hpp"
#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <cstdio>
#include <vector>

namespace {

using bit_exact::same;
using terrain::HeightmapRegion;

bool same(const terrain::SurfaceSite& a, const terrain::SurfaceSite& b) {
    return same(a.ground, b.ground) && same(a.waterLevel, b.waterLevel) && a.water == b.water &&
           same(a.gradX, b.gradX) && same(a.gradZ, b.gradZ) && same(a.steepness, b.steepness) &&
           same(a.riverDistance, b.riverDistance) &&
           same(a.continentalness, b.continentalness) && a.rockAboveAir == b.rockAboveAir &&
           a.cliffWall == b.cliffWall;
}

// The map of the plants of the LOD 0 tile of the chunk (terrain_tiles.cpp).
HeightmapRegion plantMap(terrain::ChunkCoord chunk) {
    const double x0 = chunk.x * terrain::kChunkSizeMeters;
    const double z0 = chunk.z * terrain::kChunkSizeMeters;
    return {x0 + 8.0, z0 + 8.0, 4.0, 6};
}

std::vector<HeightmapRegion> regions(const terrain::Heightfield& field) {
    const terrain::WorldSize world;
    const double cornerX = world.x / 2.0;
    const double cornerZ = world.z / 2.0;
    const terrain::ChunkCoord count = terrain::worldChunks(world);
    const terrain::TerrainTiles tiles(streaming_test::kSeed);
    return {
        plantMap(streaming_test::mountainChunk(field)),
        plantMap(streaming_test::plantChunk(tiles, 10)),
        plantMap({count.x / 2 - 1, count.z / 2 - 1}), // the last chunk, at the corner
        plantMap({-count.x / 2, -count.z / 2}),       // the first one, at the opposite corner
        {0.0, 0.0, 50.0, 48},
        {0.0, 0.0, 2000.0, 48},
        {cornerX, cornerZ, 50.0, 48},
        {cornerX, cornerZ, 1.0, 33},
        {cornerX - 2.0, -cornerZ + 2.0, 4.0, 1},
        {streaming_test::kCenterX + 3.0, streaming_test::kCenterZ - 5.0, 4.0, 1},
        {12.3, -7.7, 0.3, 20},
    };
}

void checkField(const char* name, const terrain::Heightfield& field,
                const std::vector<HeightmapRegion>& list) {
    long checked = 0;
    long different = 0;
    for (const HeightmapRegion& region : list) {
        const terrain::TerrainSurface live = terrain::surfaceFromHeightfield(field, region);
        const terrain::TerrainSurface old = frozen::surfaceFromHeightfield(field, region);
        TEST_CHECK(live.sites.size() == old.sites.size());
        for (std::size_t i = 0; i < live.sites.size() && i < old.sites.size(); ++i) {
            ++checked;
            different += same(live.sites[i], old.sites[i]) ? 0 : 1;
        }
        const terrain::HeightGrid liveGrid = terrain::sampleHeightGrid(field, region);
        const terrain::HeightGrid oldGrid = frozen::sampleHeightGrid(field, region);
        TEST_CHECK(liveGrid.heights.size() == oldGrid.heights.size());
        for (std::size_t i = 0; i < liveGrid.heights.size() && i < oldGrid.heights.size(); ++i) {
            ++checked;
            different += same(liveGrid.heights[i], oldGrid.heights[i]) ? 0 : 1;
        }
    }
    std::printf("%-30s %ld sites and pixels, %ld different\n", name, checked, different);
    TEST_CHECK(different == 0);
}

} // namespace

int main() {
    const terrain::Heightfield field(streaming_test::kSeed);
    checkField("default world", field, regions(field));
    terrain::HeightfieldParams params;
    params.macroContinent = 0.25f;
    params.plates.amplitude = 0.5f;
    const terrain::Heightfield planet(streaming_test::kSeed, params);
    const terrain::WorldSize world;
    checkField("planet", planet,
               {{0.0, 0.0, 8000.0, 32},
                {world.x / 2.0 - 4000.0, world.z / 2.0 - 4000.0, 250.0, 32},
                plantMap({-77'824, -531'264})});
    return test::result();
}

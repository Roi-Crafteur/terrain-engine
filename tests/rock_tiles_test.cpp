// The rock of the streamed tiles in three dimensions (phase 12A): a tile without a face is the tile
// of the relief alone bit for bit; densityAt is the density of the rock of its point, groundAt the
// height of the relief where no face lies; the rock of a tile (synthetic faces) sets the density of
// its samples, its bounds, its plants and the rock of its vertices, and its stitched sides take
// the detail of the coarser neighbor.
#include "terrain/generation/rock_faces.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "rock_tile.hpp"
#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

namespace {

using terrain::RockColumn;
using terrain::TerrainTiles;
using terrain::TileKey;
using terrain::tile::TileColumns;

// True if a face lies at one of the columns of the tile.
bool holdsFaces(const terrain::Heightfield& field, const TileKey& key) {
    const TileColumns columns =
        terrain::tile::sampleColumns(field, terrain::tile::latticeOf(key), true);
    return !columns.rock.empty();
}

void testSameBitsWithoutFaces() {
    const TerrainTiles rock(streaming_test::kSeed);
    const TerrainTiles relief(streaming_test::kSeed, {}, {}, std::nullopt, {}, nullptr, false);
    TEST_CHECK(rock.rock() && !relief.rock());
    const terrain::ChunkCoord mountain = streaming_test::mountainChunk(rock.heightfield());
    std::size_t compared = 0;
    bool same = true;
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        for (const int dx : {-2, 0, 3}) {
            const TileKey key = terrain::tileContaining({mountain.x + dx * 8, mountain.z}, lod);
            if (holdsFaces(rock.heightfield(), key)) {
                continue;
            }
            for (const unsigned seams : {0U, terrain::kSeamPosX | terrain::kSeamNegXNegZ}) {
                same = same && rock.generate({key, seams}) == relief.generate({key, seams});
                ++compared;
            }
        }
    }
    TEST_CHECK(compared > 0 && same);
}

void testDensityAndGround() {
    const TerrainTiles tiles(streaming_test::kSeed);
    const terrain::Heightfield& field = tiles.heightfield();
    bool density = true;
    bool ground = true;
    for (const auto& [x, z] : {std::pair(0, 0), std::pair(-123, 456), std::pair(4096, -77)}) {
        const RockColumn c = field.rockAt(x, z);
        const float h = field.heightAt(x, z);
        for (const int dy : {-3, 0, 2}) {
            const int y = static_cast<int>(std::floor(h)) + dy;
            density = density &&
                      tiles.densityAt(x, y, z) ==
                          terrain::rockDensity(c, y, terrain::rockDetailForLod(0)) &&
                      (c.faceCount > 0 ||
                       tiles.densityAt(x, y, z) == static_cast<float>(static_cast<double>(y) - h));
        }
        ground = ground && (c.faceCount > 0 || tiles.groundAt(x, z) == h);
    }
    TEST_CHECK(density);
    TEST_CHECK(ground);
}

// A tile of flat ground at 5 m with a face from 0 to 12 m along the columns i = 8, the rock beyond
// it (i > 8), seen from the columns i >= 6, its distances 1 m per column; with a cap of cap meters
// over the top 6 m.
TileColumns faceColumns(float cap) {
    TileColumns columns;
    constexpr auto kCount = static_cast<std::size_t>(terrain::kGridSamples * terrain::kGridSamples);
    columns.heights.assign(kCount, 5.0f);
    columns.continentalness.assign(kCount, 0.5f);
    columns.rock.resize(kCount);
    for (int k = 0; k < terrain::kGridSamples; ++k) {
        for (int i = 0; i < terrain::kGridSamples; ++i) {
            RockColumn& c = columns.rock[static_cast<std::size_t>(k * terrain::kGridSamples + i)];
            c.ground = 5.0f;
            c.uncarved = 5.0f;
            if (i >= 6) {
                terrain::RockFace f;
                f.foot = 0.0f;
                f.top = 12.0f;
                f.distance = static_cast<float>(8 - i);
                f.cap = cap;
                f.capDepth = 6.0f;
                c.add(f);
            }
        }
    }
    columns.minHeight = 5.0f;
    columns.maxHeight = 5.0f;
    return columns;
}

void testSyntheticTile() {
    const TileColumns columns = faceColumns(0.0f);
    const TileKey key{{0, 0}, 0};
    const terrain::tile::RockTile tile(columns, {key, 0}, terrain::tile::latticeOf(key));
    TEST_CHECK(tile.bounds().low == 0.0f && tile.bounds().high == 12.0f);
    TEST_CHECK(!tile.resamplesSeams());
    const terrain::DensityGrid grid = tile.levelDensity(0);
    TEST_CHECK(grid.at(10, 8, 3) == -2.0f);                 // behind the face
    TEST_CHECK(grid.at(7, 8, 3) == 1.0f);                   // 1 m in front of it
    TEST_CHECK(grid.at(2, 8, 3) == static_cast<float>(8.0 - 5.0)); // no face: y - ground
    std::vector<terrain::Plant> plants(2);
    plants[0].x = 2.0;
    plants[1].x = 10.0;
    tile.dropPlants(plants);
    TEST_CHECK(plants.size() == 1 && plants[0].x == 2.0);
    terrain::Mesh mesh;
    mesh.vertices = {{8.0f, 6.0f, 3.0f}, {2.0f, 5.0f, 3.0f}};
    const std::vector<float> rock = tile.vertexRock(mesh, 0.0);
    TEST_CHECK(rock.size() == 2 && rock[0] == 1.0f && rock[1] == 0.0f);
}

// On a LOD 2 tile stitched to a LOD 3 one, the even samples of the stitched side leave out the cap
// the coarser tile cannot hold; elsewhere they keep it.
void testStitchedDetail() {
    const TileColumns columns = faceColumns(3.0f);
    const TileKey key{{0, 0}, 2};
    const terrain::tile::RockTile stitched(columns, {key, terrain::kSeamNegZ},
                                           terrain::tile::latticeOf(key));
    const terrain::tile::RockTile alone(columns, {key, 0}, terrain::tile::latticeOf(key));
    TEST_CHECK(stitched.resamplesSeams() && !alone.resamplesSeams());
    // Sample (6, 2, 0): 8 m up, under the cap, 2 m in front of the face.
    const int originY = 0;
    TEST_CHECK(stitched.sample(6, 2, 0, originY, false) < 0.0f);
    TEST_CHECK(stitched.sample(6, 2, 0, originY, true) == 2.0f);
    TEST_CHECK(stitched.levelDensity(originY).at(6, 2, 0) == 2.0f);
    TEST_CHECK(stitched.levelDensity(originY).at(6, 2, 1) < 0.0f); // off the side
    TEST_CHECK(alone.levelDensity(originY).at(6, 2, 0) < 0.0f);
}

} // namespace

int main() {
    testSameBitsWithoutFaces();
    testDensityAndGround();
    testSyntheticTile();
    testStitchedDetail();
    return test::result();
}

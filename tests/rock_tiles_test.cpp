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

#include <algorithm>
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

// A tile whose relief climbs from 0 m to top meters across a riser of run meters along x, its
// middle at the columns i = 8 (meters per column apart), the rock beyond it (i > 8), and a face
// from 0 m to top standing at its middle, with a cap of cap meters over its top capDepth meters.
TileColumns faceColumns(float run, float metersPerColumn, float cap, float top = 12.0f,
                        float capDepth = 6.0f) {
    TileColumns columns;
    constexpr auto kCount = static_cast<std::size_t>(terrain::kGridSamples * terrain::kGridSamples);
    columns.continentalness.assign(kCount, 0.5f);
    columns.rock.resize(kCount);
    for (int k = 0; k < terrain::kGridSamples; ++k) {
        for (int i = 0; i < terrain::kGridSamples; ++i) {
            const auto n = static_cast<std::size_t>(k * terrain::kGridSamples + i);
            RockColumn& c = columns.rock[n];
            const float distance = static_cast<float>(8 - i) * metersPerColumn;
            c.ground = std::clamp(0.5f * top - top * distance / run, 0.0f, top);
            c.uncarved = c.ground;
            columns.heights.push_back(c.ground);
            if (std::abs(distance) <= 0.5f * run) {
                terrain::RockFace f;
                f.foot = 0.0f;
                f.top = top;
                f.distance = distance;
                f.run = run;
                f.cap = cap;
                f.capDepth = capDepth;
                c.add(f);
            }
        }
    }
    columns.minHeight = 0.0f;
    columns.maxHeight = top;
    return columns;
}

bool near(double a, double b) {
    return std::abs(a - b) < 1e-3;
}

void testSyntheticTile() {
    const TileColumns columns = faceColumns(8.0f, 1.0f, 0.0f);
    const TileKey key{{0, 0}, 0};
    const terrain::tile::RockTile tile(columns, {key, 0}, terrain::tile::latticeOf(key));
    TEST_CHECK(near(tile.bounds().low, 0.0) && near(tile.bounds().high, 12.0));
    TEST_CHECK(!tile.resamplesSeams());
    const terrain::DensityGrid grid = tile.levelDensity(0);
    TEST_CHECK(grid.at(10, 8, 3) < -1.5f && grid.at(10, 8, 3) > -2.5f); // 2 m behind the face
    TEST_CHECK(near(grid.at(7, 8, 3), 1.0));                           // 1 m in front of it
    TEST_CHECK(grid.at(2, 8, 3) == 8.0f); // no face: y - ground
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
// the coarser tile cannot hold; the density turns to that of the tile's own detail over 4 cells
// from the side, the densities of both details blended (a detail changing at once would leave a
// blade of rock along the side, a detail between them a cap neither tile draws); farther, and on
// a tile alone, they keep it. The face is 40 m high, its cap 11.5 m deep over its top 30 m (as
// thick as 4 m cells draw it, not 8 m ones).
void testStitchedDetail() {
    const TileColumns columns = faceColumns(40.0f, 4.0f, 11.5f, 40.0f, 30.0f);
    const TileKey key{{0, 0}, 2};
    const terrain::tile::RockTile stitched(columns, {key, terrain::kSeamNegZ},
                                           terrain::tile::latticeOf(key));
    const terrain::tile::RockTile alone(columns, {key, 0}, terrain::tile::latticeOf(key));
    TEST_CHECK(stitched.resamplesSeams() && !alone.resamplesSeams());
    // Sample (6, 6, 0) of the side: 24 m up, under the cap, 8 m in front of the face; the coarse
    // detail leaves the cap out.
    const int originY = 0;
    const float coarse = terrain::rockDensity(columns.rock[6], 24.0, terrain::rockDetailForLod(3));
    TEST_CHECK(stitched.sample(6, 6, 0, originY, false) < 0.0f && coarse > 0.0f);
    TEST_CHECK(stitched.sample(6, 6, 0, originY, true) == coarse);
    TEST_CHECK(stitched.levelDensity(originY).at(6, 6, 0) == coarse);
    // Off the side, the densities of both details blended, the share of the tile's growing.
    const terrain::DensityGrid level = stitched.levelDensity(originY);
    bool turning = stitched.fineShare(6, 0) == 0.0 && stitched.fineShare(6, 4) == 1.0;
    for (int k = 1; k <= 4; ++k) {
        const double share = stitched.fineShare(6, k);
        const float fine = stitched.sample(6, 6, k, originY, false);
        const float coarser = stitched.sample(6, 6, k, originY, true);
        turning = turning && share > stitched.fineShare(6, k - 1) &&
                  stitched.fineShare(3, k) == share &&
                  std::abs(level.at(6, 6, k) - (coarser + share * (fine - coarser))) < 1e-4f;
    }
    TEST_CHECK(turning);
    TEST_CHECK(level.at(6, 6, 4) < 0.0f); // 4 cells off the side
    TEST_CHECK(alone.levelDensity(originY).at(6, 6, 0) < 0.0f);
    TEST_CHECK(alone.fineShare(6, 0) == 1.0);
}

} // namespace

int main() {
    testSameBitsWithoutFaces();
    testDensityAndGround();
    testSyntheticTile();
    testStitchedDetail();
    return test::result();
}

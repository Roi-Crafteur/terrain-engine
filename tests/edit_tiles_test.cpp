// Volume edits in the streamed world: a crater across the corner of 4 chunks leaves the same
// vertices on the sides the LOD 0 tiles share; a crater and a razed area across the edge of two
// LODs stay stitched; a sphere across the edge x = world.x / 2 digs both sides alike; the
// generated plants standing on removed ground are dropped, and come back when it is restored,
// the tiles without edits keeping their bits; the surface blend of an edited tile stays that of
// the natural ground.
#include "terrain/storage/edit_window.hpp"
#include "terrain/storage/volume_edit.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "seam_test_support.hpp"
#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using seam_test::FacePoint;
using seam_test::Plane;
using terrain::BlockCoord;
using terrain::ChunkCoord;
using terrain::EditMode;
using terrain::EditShape;
using terrain::TerrainTiles;
using terrain::TileData;
using terrain::TileKey;
using terrain::VolumeEdit;

constexpr double kSeamTolerance = 1e-5;

int groundAt(const TerrainTiles& tiles, int x, int z) {
    return static_cast<int>(std::floor(tiles.heightfield().heightAt(x, z)));
}

VolumeEdit sphere(EditShape shape, BlockCoord center, double radius) {
    return {shape, EditMode::Remove, center, {}, static_cast<std::uint16_t>(radius * 16.0)};
}

std::vector<FacePoint> sortedFace(const TileData& data, Plane plane) {
    std::vector<FacePoint> points = seam_test::facePoints(data, plane);
    std::sort(points.begin(), points.end(), [](FacePoint p, FacePoint q) {
        return p.u < q.u || (p.u == q.u && p.y < q.y);
    });
    return points;
}

bool sameFace(const TileData& a, const TileData& b, Plane plane) {
    const std::vector<FacePoint> p = sortedFace(a, plane);
    const std::vector<FacePoint> q = sortedFace(b, plane);
    bool same = !p.empty() && p.size() == q.size();
    for (std::size_t i = 0; same && i < p.size(); ++i) {
        same = p[i].u == q[i].u && std::abs(p[i].y - q[i].y) <= 1e-4;
    }
    return same;
}

// A crater across the corner of 4 chunks: each pair of neighbors shares its side.
void testCraterAcrossChunks() {
    TerrainTiles tiles(streaming_test::kSeed);
    const ChunkCoord c{streaming_test::kCenterChunkX, streaming_test::kCenterChunkZ};
    const int x0 = c.x * 16;
    const int z0 = c.z * 16;
    std::vector<TileData> before;
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        before.push_back(tiles.generate({terrain::tileContaining(c, lod), 0}));
    }
    const std::vector<ChunkCoord> chunks =
        tiles.applyEdit(sphere(EditShape::Explosion, {x0, groundAt(tiles, x0, z0), z0}, 7.0));
    TEST_CHECK(chunks == (std::vector<ChunkCoord>{{c.x - 1, c.z - 1}, {c.x, c.z - 1},
                                                  {c.x - 1, c.z}, {c.x, c.z}}));
    const TileData sw = tiles.generate({{{c.x - 1, c.z - 1}, 0}, 0});
    const TileData se = tiles.generate({{{c.x, c.z - 1}, 0}, 0});
    const TileData nw = tiles.generate({{{c.x - 1, c.z}, 0}, 0});
    const TileData ne = tiles.generate({{c, 0}, 0});
    TEST_CHECK(!(ne.mesh == before[0].mesh));
    const Plane alongX{true, static_cast<double>(x0)};
    const Plane alongZ{false, static_cast<double>(z0)};
    TEST_CHECK(sameFace(sw, se, alongX) && sameFace(nw, ne, alongX));
    TEST_CHECK(sameFace(sw, nw, alongZ) && sameFace(se, ne, alongZ));
    // The crater reaches lattice points of every LOD.
    for (int lod = 1; lod < terrain::kLodCount; ++lod) {
        const TileData tile = tiles.generate({terrain::tileContaining(c, lod), 0});
        TEST_CHECK(!(tile.mesh == before[static_cast<std::size_t>(lod)].mesh));
        // The surface blend is that of the natural ground: the edits never change it.
        TEST_CHECK(!tile.surfaceBlend.empty() &&
                   tile.surfaceBlend == before[static_cast<std::size_t>(lod)].surfaceBlend);
    }
}

// The stitched side of the fine tile and the side of the coarse tile meet: every coarse point on
// the outline of the fine side, every fine point within fineTolerance of the coarse outline.
void checkSeam(const TerrainTiles& tiles, TileKey fine, TileKey coarse, unsigned seam,
               Plane plane, double fineTolerance) {
    const TileData coarseTile = tiles.generate({coarse, 0});
    const TileData stitched = tiles.generate({fine, seam});
    const double first = (plane.alongX ? fine.origin.z : fine.origin.x) * terrain::kChunkSizeMeters;
    const double last = first + terrain::tileSizeMeters(fine.lod);
    std::size_t finePoints = 0;
    std::size_t coarsePoints = 0;
    const double fineGap =
        seam_test::largestGap(stitched, coarseTile, plane, first, last, finePoints);
    const double coarseGap =
        seam_test::largestGap(coarseTile, stitched, plane, first, last, coarsePoints);
    std::printf("seam LOD %d / %d: %zu and %zu points, gaps %.2g and %.2g m\n", fine.lod,
                coarse.lod, finePoints, coarsePoints, fineGap, coarseGap);
    TEST_CHECK(finePoints > coarsePoints && coarsePoints >= 2);
    TEST_CHECK(fineGap <= fineTolerance && coarseGap <= kSeamTolerance);
}

// A crater, then a razed area, on the edge between a LOD 0 tile and the LOD 1 tile west of it.
// The stitching interpolates the fine side bilinearly within each coarse cell of the face, where
// Marching Cubes draws a straight segment: on smooth ground, flat floors and straight walls they
// agree, but around the curved edge of a crater, whose density is not bilinear, the fine side
// leaves the coarse one by up to a few tenths of a meter, as it did block by block before the
// sections (the same densities, bit for bit).
void testAcrossLods() {
    TerrainTiles tiles(streaming_test::kSeed);
    const ChunkCoord m = streaming_test::mountainChunk(tiles.heightfield());
    const TileKey fine{{m.x + 2, m.z}, 0};
    const TileKey coarse{{m.x, m.z}, 1};
    const int edge = (m.x + 2) * 16;
    const Plane plane{true, static_cast<double>(edge)};
    const int z = m.z * 16 + 9;
    tiles.applyEdit(sphere(EditShape::Sphere, {edge, groundAt(tiles, edge, z), z}, 6.0));
    checkSeam(tiles, fine, coarse, terrain::kSeamNegX, plane, 0.5);
    // The mountain razed 30 m below its lowest ground there, over 48 x 32 m across the edge.
    int lowest = groundAt(tiles, edge, z);
    for (int dz = 0; dz < 32; dz += 4) {
        for (int dx = -24; dx < 24; dx += 4) {
            lowest = std::min(lowest, groundAt(tiles, edge + dx, m.z * 16 + dz));
        }
    }
    const VolumeEdit razed{EditShape::Box, EditMode::Remove, {edge - 24, lowest - 30, m.z * 16},
                           {48, 400, 32}, 0};
    const TileData fineBefore = tiles.generate({fine, terrain::kSeamNegX});
    tiles.applyEdit(razed);
    checkSeam(tiles, fine, coarse, terrain::kSeamNegX, plane, kSeamTolerance);
    TEST_CHECK(tiles.densityAt(edge, lowest - 20, m.z * 16 + 5) > 0.0f);
    // Razed, the tile keeps the surface blend of the natural ground.
    const TileData fineAfter = tiles.generate({fine, terrain::kSeamNegX});
    TEST_CHECK(!(fineAfter.mesh == fineBefore.mesh) &&
               fineAfter.surfaceBlend == fineBefore.surfaceBlend);
}

// A sphere across the edge x = world.x / 2, given at the eastern image: both sides dug, their
// faces on the edge identical.
void testAcrossTheEdge() {
    TerrainTiles tiles(streaming_test::kSeed);
    const ChunkCoord count = terrain::worldChunks(terrain::WorldSize{});
    constexpr int kRow = -641774;
    const int edge = count.x * 8;
    const int z = kRow * 16 + 8;
    const TileData eastBefore = tiles.generate({{{count.x / 2 - 1, kRow}, 0}, 0});
    tiles.applyEdit(sphere(EditShape::Sphere, {edge + count.x * 16, groundAt(tiles, edge, z), z},
                           5.0));
    const TileData east = tiles.generate({{{count.x / 2 - 1, kRow}, 0}, 0});
    const TileData west = tiles.generate({{{-count.x / 2, kRow}, 0}, 0});
    TEST_CHECK(!(east.mesh == eastBefore.mesh));
    const std::vector<FacePoint> e = sortedFace(east, {true, static_cast<double>(edge)});
    const std::vector<FacePoint> w = sortedFace(west, {true, static_cast<double>(-edge)});
    bool same = !e.empty() && e.size() == w.size();
    for (std::size_t i = 0; same && i < e.size(); ++i) {
        same = e[i].u == w[i].u && std::abs(e[i].y - w[i].y) <= 1e-4;
    }
    TEST_CHECK(same);
}

bool holds(const std::vector<terrain::Plant>& plants, const terrain::Plant& plant) {
    return std::find(plants.begin(), plants.end(), plant) != plants.end();
}

// The plants of a chunk with their ground removed, the air above removed, then restored.
void testPlants() {
    TerrainTiles tiles(streaming_test::kSeed);
    const ChunkCoord chunk = streaming_test::plantChunk(tiles, 3);
    const TileData before = tiles.generate({{chunk, 0}, 0});
    const TileData neighbor = tiles.generate({{{chunk.x + 3, chunk.z}, 0}, 0});
    TEST_CHECK(before.plants.size() >= 3);
    if (before.plants.size() < 3) {
        return;
    }
    const terrain::Plant dug = before.plants.front();
    const terrain::Plant kept = before.plants.back();
    const BlockCoord foot = terrain::plantFootBlock(dug);
    const BlockCoord above = terrain::plantFootBlock(kept);
    tiles.applyEdit({EditShape::Box, EditMode::Remove, foot, {1, 1, 1}, 0});
    tiles.applyEdit({EditShape::Box, EditMode::Remove, {above.x, above.y + 2, above.z}, {1, 3, 1},
                     0});
    const TileData after = tiles.generate({{chunk, 0}, 0});
    TEST_CHECK(!holds(after.plants, dug) && holds(after.plants, kept));
    TEST_CHECK(after.plants.size() + 1 == before.plants.size());
    TEST_CHECK(tiles.generate({{{chunk.x + 3, chunk.z}, 0}, 0}) == neighbor);
    tiles.applyEdit({EditShape::Box, EditMode::Restore, {foot.x - 40, foot.y - 40, foot.z - 40},
                     {80, 80, 80}, 0});
    TEST_CHECK(tiles.modifications().empty());
    TEST_CHECK(tiles.generate({{chunk, 0}, 0}) == before);
}

} // namespace

int main() {
    testCraterAcrossChunks();
    testAcrossLods();
    testAcrossTheEdge();
    testPlants();
    return test::result();
}

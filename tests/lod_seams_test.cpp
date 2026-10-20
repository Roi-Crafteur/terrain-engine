// Seams between LODs on a mountain of the default world: where a tile borders a coarser one,
// every vertex of its stitched side lies on the side of the coarser mesh and the reverse, so
// the meshes meet without a crack; without stitching they do not. Tiles of the same LOD share
// their side exactly.
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "seam_test_support.hpp"
#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace {

using terrain::ChunkCoord;
using terrain::TerrainTiles;
using terrain::TileData;
using terrain::TileKey;
using terrain::TileRequest;

// Largest gap allowed between the two sides of a seam, meters: float rounding only (about
// 1e-7 m measured), while the cracks of unstitched sides measure millimeters.
constexpr double kSeamTolerance = 1e-5;

using seam_test::FacePoint;
using seam_test::facePoints;
using seam_test::largestGap;
using seam_test::Plane;

// A fine tile with a coarser neighbor across the side `seam`, which lies on plane.
struct SeamCase {
    TileKey fine;
    TileKey coarse;
    unsigned seam = 0;
    Plane plane;
};

// Range of the shared side along u: the side of the fine tile.
std::pair<double, double> sharedRange(const SeamCase& c) {
    const double first = (c.plane.alongX ? c.fine.origin.z : c.fine.origin.x) *
                         terrain::kChunkSizeMeters;
    return {first, first + terrain::tileSizeMeters(c.fine.lod)};
}

void checkSeam(const TerrainTiles& tiles, const SeamCase& c) {
    const TileData coarse = tiles.generate({c.coarse, 0});
    const TileData stitched = tiles.generate({c.fine, c.seam});
    const TileData raw = tiles.generate({c.fine, 0});
    const auto [uMin, uMax] = sharedRange(c);

    std::size_t finePoints = 0;
    std::size_t coarsePoints = 0;
    const double fineGap = largestGap(stitched, coarse, c.plane, uMin, uMax, finePoints);
    const double coarseGap = largestGap(coarse, stitched, c.plane, uMin, uMax, coarsePoints);
    TEST_CHECK(finePoints > coarsePoints && coarsePoints >= 2);
    TEST_CHECK(fineGap <= kSeamTolerance);
    TEST_CHECK(coarseGap <= kSeamTolerance);

    // The real density between the coarse samples is not linear: unstitched, the fine side
    // leaves the coarse one and the meshes crack.
    std::size_t rawPoints = 0;
    const double rawGap = largestGap(raw, coarse, c.plane, uMin, uMax, rawPoints);
    TEST_CHECK(rawGap > 10.0 * kSeamTolerance);
}

void testSeamsBetweenLods() {
    const TerrainTiles tiles(streaming_test::kSeed);
    const ChunkCoord m = streaming_test::mountainChunk(tiles.heightfield());
    TEST_CHECK(m.x % 64 == 0 && m.z % 64 == 0);
    const double chunk = terrain::kChunkSizeMeters;
    const SeamCase cases[] = {
        // LOD 0 east of a LOD 1 tile, LOD 1 north of a LOD 2 tile, LOD 2 west of a LOD 3 tile.
        {{{m.x + 2, m.z}, 0}, {{m.x, m.z}, 1}, terrain::kSeamNegX, {true, (m.x + 2) * chunk}},
        {{{m.x, m.z + 2}, 1}, {{m.x, m.z + 4}, 2}, terrain::kSeamPosZ, {false, (m.z + 4) * chunk}},
        {{{m.x + 4, m.z}, 2}, {{m.x + 8, m.z}, 3}, terrain::kSeamPosX, {true, (m.x + 8) * chunk}},
    };
    for (const SeamCase& c : cases) {
        checkSeam(tiles, c);
    }
}

// Two tiles of the same LOD sample their shared side identically: same vertices on it.
void testSameLodSides() {
    const TerrainTiles tiles(streaming_test::kSeed);
    const ChunkCoord m = streaming_test::mountainChunk(tiles.heightfield());
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        const int size = terrain::tileChunks(lod);
        const TileData west = tiles.generate({{{m.x, m.z}, lod}, 0});
        const TileData east = tiles.generate({{{m.x + size, m.z}, lod}, 0});
        const Plane plane{true, (m.x + size) * terrain::kChunkSizeMeters};
        std::vector<FacePoint> a = facePoints(west, plane);
        std::vector<FacePoint> b = facePoints(east, plane);
        const auto order = [](FacePoint p, FacePoint q) {
            return p.u < q.u || (p.u == q.u && p.y < q.y);
        };
        std::sort(a.begin(), a.end(), order);
        std::sort(b.begin(), b.end(), order);
        bool same = !a.empty() && a.size() == b.size();
        for (std::size_t i = 0; same && i < a.size(); ++i) {
            same = a[i].u == b[i].u && std::abs(a[i].y - b[i].y) <= 1e-4;
        }
        TEST_CHECK(same);
    }
}

} // namespace

int main() {
    testSeamsBetweenLods();
    testSameLodSides();
    return test::result();
}

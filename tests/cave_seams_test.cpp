// Seams between streamed tiles where a cave crosses their shared side: the tube of an entrance
// of the world with caves. Tiles of the same LOD share their side exactly. Between LODs, the
// fine side holds the coarse samples carved with the coarser detail: every coarse vertex lies
// on the fine outline and every fine vertex on the coarse lattice lines on the coarse outline;
// in between, the density is not linear in y inside the caves and stitching leaves gaps of at
// most half a coarse cell (a known limit; the ground away from the caves stays exact, see
// lod_seams_test.cpp).
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "seam_test_support.hpp"
#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <optional>
#include <vector>

namespace {

using seam_test::FacePoint;
using seam_test::Plane;
using terrain::CaveEntrance;
using terrain::ChunkCoord;
using terrain::TerrainTiles;
using terrain::TileData;
using terrain::TileKey;

// Float rounding only, as in lod_seams_test.cpp.
constexpr double kSeamTolerance = 1e-5;

int floorTo(double meters, int multipleChunks) {
    const auto chunks = static_cast<int>(std::floor(meters / terrain::kChunkSizeMeters));
    return static_cast<int>(std::floor(static_cast<double>(chunks) / multipleChunks)) *
           multipleChunks;
}

// A vertical plane on the lattice of tiles of `chunks` chunks crossing the tube of the
// entrance where its axis lies at least `minDepth` below the ground, and the point of the
// axis there.
struct Crossing {
    Plane plane;
    double u = 0.0; // along the plane
};

std::optional<Crossing> crossing(const TerrainTiles& tiles, const CaveEntrance& e, int chunks,
                                 double minDepth) {
    const double step = chunks * terrain::kChunkSizeMeters;
    for (const bool alongX : {true, false}) {
        const double from = alongX ? e.topX : e.topZ;
        const double to = alongX ? e.bottomX : e.bottomZ;
        const double first = std::ceil(std::min(from, to) / step) * step;
        for (double at = first; at <= std::max(from, to); at += step) {
            const double t = (at - from) / (to - from);
            const double x = e.topX + t * (e.bottomX - e.topX);
            const double y = e.topY + t * (e.bottomY - e.topY);
            const double z = e.topZ + t * (e.bottomZ - e.topZ);
            if (tiles.heightfield().heightAt(x, z) - y >= minDepth) {
                return Crossing{{alongX, at}, alongX ? z : x};
            }
        }
    }
    return std::nullopt;
}

// Tile of LOD lod on the far side of the plane (east or north of it) holding the crossing,
// and its neighbor of LOD neighborLod on the near side.
struct TilePair {
    TileKey far;
    TileKey near;
    unsigned farSeam = 0; // the side of the far tile on the plane
};

TilePair tilesAround(const Crossing& c, int lod, int neighborLod) {
    const int farChunks = terrain::tileChunks(lod);
    const int nearChunks = terrain::tileChunks(neighborLod);
    const auto at = static_cast<int>(std::lround(c.plane.at / terrain::kChunkSizeMeters));
    const int farU = floorTo(c.u, farChunks);
    const int nearU = floorTo(c.u, nearChunks);
    if (c.plane.alongX) {
        return {{{at, farU}, lod}, {{at - nearChunks, nearU}, neighborLod}, terrain::kSeamNegX};
    }
    return {{{farU, at}, lod}, {{nearU, at - nearChunks}, neighborLod}, terrain::kSeamNegZ};
}

// Number of points deeper than depth below the ground.
std::size_t deeperThan(const TerrainTiles& tiles, const std::vector<FacePoint>& points,
                       const Plane& plane, double depth) {
    return static_cast<std::size_t>(std::count_if(points.begin(), points.end(), [&](FacePoint p) {
        const double x = plane.alongX ? plane.at : p.u;
        const double z = plane.alongX ? p.u : plane.at;
        return tiles.heightfield().heightAt(x, z) - p.y > depth;
    }));
}

std::vector<FacePoint> inRange(const std::vector<FacePoint>& points, double uMin, double uMax) {
    std::vector<FacePoint> kept;
    std::copy_if(points.begin(), points.end(), std::back_inserter(kept),
                 [&](FacePoint p) { return p.u >= uMin && p.u <= uMax; });
    return kept;
}

// Two tiles of the same LOD on both sides of a plane crossing the tube: the same vertices on
// the plane, the cave included.
void checkSameLod(const TerrainTiles& tiles, const Crossing& c, int lod) {
    const TilePair pair = tilesAround(c, lod, lod);
    const TileData far = tiles.generate({pair.far, 0});
    const TileData near = tiles.generate({pair.near, 0});
    std::vector<FacePoint> a = seam_test::facePoints(far, c.plane);
    std::vector<FacePoint> b = seam_test::facePoints(near, c.plane);
    const auto order = [](FacePoint p, FacePoint q) {
        return p.u < q.u || (p.u == q.u && p.y < q.y);
    };
    std::sort(a.begin(), a.end(), order);
    std::sort(b.begin(), b.end(), order);
    bool same = !a.empty() && a.size() == b.size();
    for (std::size_t i = 0; same && i < a.size(); ++i) {
        same = a[i].u == b[i].u && std::abs(a[i].y - b[i].y) <= 1e-4;
    }
    const std::size_t cave = deeperThan(tiles, a, c.plane, tiles.caves()->params().minRoof);
    std::printf("LOD %d | LOD %d: %zu vertices on the side, %zu in the cave\n", lod, lod,
                a.size(), cave);
    TEST_CHECK(same);
    TEST_CHECK(cave > 0);
}

bool onCoarseLattice(FacePoint p, double coarseCell) {
    return std::fmod(p.u, coarseCell) == 0.0 || std::fmod(p.y, coarseCell) == 0.0;
}

// A tile of LOD lod stitched to a coarser neighbor across a plane crossing the tube.
void checkStitched(const TerrainTiles& tiles, const Crossing& c, int lod) {
    const TilePair pair = tilesAround(c, lod, lod + 1);
    const TileData coarse = tiles.generate({pair.near, 0});
    const TileData stitched = tiles.generate({pair.far, pair.farSeam});
    const TileData raw = tiles.generate({pair.far, 0});
    const double uMin = (c.plane.alongX ? pair.far.origin.z : pair.far.origin.x) *
                        terrain::kChunkSizeMeters;
    const double uMax = uMin + terrain::tileSizeMeters(lod);
    const double coarseCell = terrain::tileCellMeters(lod + 1);

    const std::vector<seam_test::Segment> fineOutline =
        seam_test::faceSegments(stitched, c.plane);
    const std::vector<seam_test::Segment> coarseOutline = seam_test::faceSegments(coarse, c.plane);
    double coarseGap = 0.0;
    for (const FacePoint p : inRange(seam_test::facePoints(coarse, c.plane), uMin, uMax)) {
        coarseGap = std::max(coarseGap, seam_test::distanceToOutline(p, fineOutline));
    }
    double latticeGap = 0.0;
    double otherGap = 0.0;
    const std::vector<FacePoint> fine =
        inRange(seam_test::facePoints(stitched, c.plane), uMin, uMax);
    for (const FacePoint p : fine) {
        double& worst = onCoarseLattice(p, coarseCell) ? latticeGap : otherGap;
        worst = std::max(worst, seam_test::distanceToOutline(p, coarseOutline));
    }
    const double minRoof = tiles.caves()->params().minRoof;
    const std::size_t fineCave = deeperThan(tiles, fine, c.plane, minRoof);
    const std::size_t rawCave = deeperThan(
        tiles, inRange(seam_test::facePoints(raw, c.plane), uMin, uMax), c.plane, minRoof);
    std::printf("LOD %d | LOD %d: coarse gap %.2g, fine gap %.2g on the coarse lattice, %.3f "
                "elsewhere (%.1f m cells); %zu fine vertices in the cave (%zu unstitched)\n",
                lod, lod + 1, coarseGap, latticeGap, otherGap, coarseCell, fineCave, rawCave);
    TEST_CHECK(fine.size() >= 2);
    TEST_CHECK(coarseGap <= kSeamTolerance);
    TEST_CHECK(latticeGap <= kSeamTolerance);
    TEST_CHECK(otherGap <= 0.5 * coarseCell);
    TEST_CHECK(rawCave > 0);
    // The far LOD 3 carves nothing: its stitched neighbor closes the cave on their side.
    TEST_CHECK(lod + 1 == terrain::kMaxLod ? fineCave == 0 : fineCave > 0);
}

// For each LOD, the first entrance near the origin whose tube crosses a plane of the tile
// lattice of the coarser LOD at least the minimum roof below the ground.
void testSeams(const TerrainTiles& tiles) {
    const std::vector<CaveEntrance> entrances = streaming_test::entrancesNearOrigin(tiles);
    const double minDepth = tiles.caves()->params().minRoof + 4.0;
    for (int lod = 0; lod < terrain::kMaxLod; ++lod) {
        std::optional<Crossing> found;
        for (const CaveEntrance& e : entrances) {
            found = crossing(tiles, e, terrain::tileChunks(lod + 1), minDepth);
            if (found) {
                break;
            }
        }
        TEST_CHECK(found.has_value());
        if (found) {
            checkSameLod(tiles, *found, lod);
            checkStitched(tiles, *found, lod);
        }
    }
}

} // namespace

int main() {
    const TerrainTiles tiles(streaming_test::kSeed, {}, {}, terrain::CaveParams{});
    testSeams(tiles);
    return test::result();
}

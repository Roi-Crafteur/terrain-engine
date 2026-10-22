// Seams of the streamed tiles with cave districts (Phase 8C-1), on the legacy relief (the places
// of the streaming tests, test_relief::legacyRelief, as the other tests of the districts), where
// the tube of an entrance crosses the shared side of two tiles: at the entrances of the main
// district types (crevasses, halls, volcanic), in the border zones between districts, and over
// small districts. Tiles of the same LOD share their side exactly. A tile stitched to a coarser one
// holds the samples of that tile, carved with its detail (cave_seams_test.cpp, without
// districts): every coarse vertex lies on the fine outline, every fine vertex on the coarse
// lattice lines on the coarse outline, and the others within half a coarse cell, the known limit
// of the coarse caves.
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "cave_districts_carving_support.hpp"
#include "cave_districts_streaming_support.hpp"
#include "legacy_relief_support.hpp"
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

using districts_test::DistrictEntrance;
using seam_test::FacePoint;
using seam_test::Plane;
using streaming_test::kSeed;
using terrain::CaveDistrictType;
using terrain::CaveEntrance;
using terrain::TerrainTiles;
using terrain::TileData;
using terrain::TileKey;

// ---------------------------------------------------------------------------

int floorTo(double meters, int multipleChunks) {
    const auto chunks = static_cast<int>(std::floor(meters / terrain::kChunkSizeMeters));
    return static_cast<int>(std::floor(static_cast<double>(chunks) / multipleChunks)) *
           multipleChunks;
}

// A vertical plane of the lattice of tiles of `chunks` chunks crossing the tube of an entrance
// where its axis lies at least minDepth below the ground, and the point of the axis there.
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
        for (double at = std::ceil(std::min(from, to) / step) * step; at <= std::max(from, to);
             at += step) {
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

// The tile of LOD lod east or north of the plane holding the crossing, and its neighbor of LOD
// neighborLod on the near side.
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

// Two tiles of the same LOD on both sides of a plane crossing the tube: the very same vertices on
// the plane, the cave included.
void checkSameLod(const TerrainTiles& tiles, const Crossing& c, int lod) {
    const TilePair pair = tilesAround(c, lod, lod);
    std::vector<FacePoint> a = seam_test::facePoints(tiles.generate({pair.far, 0}), c.plane);
    std::vector<FacePoint> b = seam_test::facePoints(tiles.generate({pair.near, 0}), c.plane);
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
    TEST_CHECK(same);
    TEST_CHECK(cave > 0);
}

bool onCoarseLattice(FacePoint p, double coarseCell) {
    return std::fmod(p.u, coarseCell) == 0.0 || std::fmod(p.y, coarseCell) == 0.0;
}

// Float rounding only, as in lod_seams_test.cpp.
constexpr double kSeamTolerance = 1e-5;

// A tile of LOD lod stitched to a coarser neighbor across a plane crossing the tube: every coarse
// vertex lies on the fine outline, every fine vertex of the coarse lattice lines on the coarse
// outline, and the others within half a coarse cell (the known limit of the coarse caves).
void checkStitched(const TerrainTiles& tiles, const Crossing& c, int lod) {
    const TilePair pair = tilesAround(c, lod, lod + 1);
    const TileData coarse = tiles.generate({pair.near, 0});
    const TileData stitched = tiles.generate({pair.far, pair.farSeam});
    const TileData raw = tiles.generate({pair.far, 0});
    const double uMin =
        (c.plane.alongX ? pair.far.origin.z : pair.far.origin.x) * terrain::kChunkSizeMeters;
    const double uMax = uMin + terrain::tileSizeMeters(lod);
    const double coarseCell = terrain::tileCellMeters(lod + 1);
    const auto fineOutline = seam_test::faceSegments(stitched, c.plane);
    const auto coarseOutline = seam_test::faceSegments(coarse, c.plane);
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
    std::printf(
        "    LOD %d | LOD %d: coarse gap %.2g, fine gap %.2g on the lattice, %.3f elsewhere "
        "(%.0f m cells), %zu fine vertices in the cave\n",
        lod, lod + 1, coarseGap, latticeGap, otherGap, coarseCell, fineCave);
    TEST_CHECK(fine.size() >= 2 && rawCave > 0);
    TEST_CHECK(coarseGap <= kSeamTolerance && latticeGap <= kSeamTolerance);
    TEST_CHECK(otherGap <= 0.5 * coarseCell);
    // The far LOD 3 carves nothing: its stitched neighbor closes the cave on their side.
    TEST_CHECK(lod + 1 == terrain::kMaxLod ? fineCave == 0 : fineCave > 0);
}

// For each LOD, the first of the entrances whose tube crosses a plane of the tile lattice of the
// coarser LOD at least the minimum roof below the ground gives a pair of tiles of the same LOD
// and a stitched pair.
void checkSeamsAt(const TerrainTiles& tiles, const std::vector<CaveEntrance>& entrances) {
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

// No volcanic district opens within 4 km of the center of the streaming tests: the entrances
// within 1.5 km of the site of the first one on land among the 17 x 17 districts around it.
std::vector<DistrictEntrance> volcanicEntrances(const TerrainTiles& tiles) {
    const terrain::CaveField& field = *tiles.caves();
    const auto cellX =
        static_cast<int>(field.districts()->cellsX().cellOf(streaming_test::kCenterX));
    const auto cellZ =
        static_cast<int>(field.districts()->cellsZ().cellOf(streaming_test::kCenterZ));
    const std::vector<terrain::CaveDistrict> found =
        districts_test::districtsOf(field, tiles.caveGround(), {cellX - 8, 17}, {cellZ - 8, 17},
                                    CaveDistrictType::Volcanic, 5.0f);
    if (found.empty()) {
        return {};
    }
    const terrain::CaveDistrict& d = found.front();
    return districts_test::withDistricts(
        tiles, field.entrancesNear(d.siteX - 1500.0, d.siteZ - 1500.0, d.siteX + 1500.0,
                                   d.siteZ + 1500.0, tiles.caveGround()));
}

// At the entrances of the main districts (crevasses, halls, volcanic) and of the border zones.
void testSeams(const TerrainTiles& tiles, const std::vector<DistrictEntrance>& entrances) {
    const auto of = [&entrances](bool blended, std::optional<CaveDistrictType> type) {
        std::vector<CaveEntrance> list;
        for (const DistrictEntrance& d : entrances) {
            if (d.blended == blended && (!type || d.type == *type)) {
                list.push_back(d.entrance);
            }
        }
        return list;
    };
    for (const CaveDistrictType type :
         {CaveDistrictType::Crevasses, CaveDistrictType::Halls, CaveDistrictType::Volcanic}) {
        std::printf("  seams at the entrances of the %s districts\n",
                    terrain::caveDistrictName(type));
        checkSeamsAt(tiles, of(false, type));
    }
    std::printf("  seams at the entrances in the border zones\n");
    checkSeamsAt(tiles, of(true, std::nullopt));
    // Small districts (480 m, blended over 90 m): the borders are everywhere.
    const TerrainTiles dense(kSeed, test_relief::legacyRelief(), {}, districts_test::denseCaves());
    std::printf("  seams at the entrances of small districts\n");
    checkSeamsAt(dense, streaming_test::entrancesNearOrigin(dense));
}

} // namespace

int main() {
    const TerrainTiles tiles(kSeed, test_relief::legacyRelief(), {},
                             districts_test::defaultCaves());
    std::vector<DistrictEntrance> entrances = districts_test::districtEntrances(tiles);
    const std::vector<DistrictEntrance> volcanic = volcanicEntrances(tiles);
    std::printf("%zu entrances near the origin, %zu around a volcanic district\n", entrances.size(),
                volcanic.size());
    TEST_CHECK(entrances.size() >= 40 && volcanic.size() >= 10);
    entrances.insert(entrances.end(), volcanic.begin(), volcanic.end());
    testSeams(tiles, entrances);
    return test::result();
}

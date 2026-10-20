// Seams between LODs on a mountain of the default world: where a tile borders a coarser one,
// every vertex of its stitched side lies on the side of the coarser mesh and the reverse, so
// the meshes meet without a crack; without stitching they do not. Tiles of the same LOD share
// their side exactly.
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
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

// A point of a vertical plane: u along the plane, y up.
struct FacePoint {
    double u = 0.0;
    double y = 0.0;
};

// The vertical plane where the world coordinate along x (alongX) or z is plane.
struct Plane {
    bool alongX = true;
    double at = 0.0;
};

bool onPlane(const streaming_test::WorldPoint& p, Plane plane) {
    return (plane.alongX ? p.x : p.z) == plane.at;
}

FacePoint project(const streaming_test::WorldPoint& p, Plane plane) {
    return {plane.alongX ? p.z : p.x, p.y};
}

std::vector<FacePoint> facePoints(const TileData& data, Plane plane) {
    std::vector<FacePoint> points;
    for (std::size_t i = 0; i < data.mesh.vertices.size(); ++i) {
        const streaming_test::WorldPoint p = streaming_test::worldVertex(data, i);
        if (onPlane(p, plane)) {
            points.push_back(project(p, plane));
        }
    }
    return points;
}

using Segment = std::pair<FacePoint, FacePoint>;

// Edges of the triangles lying in the plane: the outline of the mesh on it.
std::vector<Segment> faceSegments(const TileData& data, Plane plane) {
    std::vector<Segment> segments;
    const std::vector<std::uint32_t>& indices = data.mesh.indices;
    for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
        for (std::size_t e = 0; e < 3; ++e) {
            const auto a = streaming_test::worldVertex(data, indices[t + e]);
            const auto b = streaming_test::worldVertex(data, indices[t + (e + 1) % 3]);
            if (onPlane(a, plane) && onPlane(b, plane)) {
                segments.emplace_back(project(a, plane), project(b, plane));
            }
        }
    }
    return segments;
}

double distanceToSegment(FacePoint p, const Segment& s) {
    const double du = s.second.u - s.first.u;
    const double dy = s.second.y - s.first.y;
    const double length2 = du * du + dy * dy;
    double t = 0.0;
    if (length2 > 0.0) {
        t = std::clamp(((p.u - s.first.u) * du + (p.y - s.first.y) * dy) / length2, 0.0, 1.0);
    }
    return std::hypot(p.u - (s.first.u + t * du), p.y - (s.first.y + t * dy));
}

// Largest distance from a point of `from` on the plane, with u in [uMin, uMax], to the outline
// of `to` on it. Also counts the points measured.
double largestGap(const TileData& from, const TileData& to, Plane plane, double uMin, double uMax,
                  std::size_t& points) {
    const std::vector<Segment> outline = faceSegments(to, plane);
    double largest = 0.0;
    points = 0;
    for (const FacePoint p : facePoints(from, plane)) {
        if (p.u < uMin || p.u > uMax) {
            continue;
        }
        double nearest = std::numeric_limits<double>::infinity();
        for (const Segment& s : outline) {
            nearest = std::min(nearest, distanceToSegment(p, s));
        }
        largest = std::max(largest, nearest);
        ++points;
    }
    return largest;
}

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

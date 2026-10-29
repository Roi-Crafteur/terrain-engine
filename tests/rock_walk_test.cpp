// The walk to the foot of the K2 (phase 12A): from the collision point of the second stop
// (415 839.2, -11 414 503.0), heading 191.3 degrees, over a ground almost flat, under the lip of a
// cap, to the vertical face about 40.6 m ahead. On the LOD 0 tiles a client takes its collision
// from (no opening within them), a walker finds its floor at every kStep meters: never a hole,
// never a step up or down of more than kStepMost, at least kHeadroom of air over it, and it meets
// the face (a rock rising more than kStepMost ahead) after kLeastWalk meters.
#include "terrain/io/mesh_checks.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <vector>

namespace {

constexpr double kStartX = 415'839.2;
constexpr double kStartZ = -11'414'503.0;
constexpr double kHeading = 191.3; // degrees clockwise from the north (-Z)
constexpr double kStep = 0.1;
constexpr double kStepMost = 0.45;
constexpr double kHeadroom = 2.0;
constexpr double kLeastWalk = 38.0;
constexpr double kMostWalk = 43.0;

// The heights of the surfaces of the tiles over (x, z): the triangles a vertical line crosses.
std::vector<double> surfacesAt(const std::vector<terrain::TileData>& tiles, double x, double z) {
    std::vector<double> out;
    for (const terrain::TileData& t : tiles) {
        const terrain::Mesh& m = t.mesh;
        const double px = x - t.originX;
        const double pz = z - t.originZ;
        for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
            const terrain::Vec3f& a = m.vertices[m.indices[i]];
            const terrain::Vec3f& b = m.vertices[m.indices[i + 1]];
            const terrain::Vec3f& c = m.vertices[m.indices[i + 2]];
            const double d0 = (b.x - a.x) * (pz - a.z) - (b.z - a.z) * (px - a.x);
            const double d1 = (c.x - b.x) * (pz - b.z) - (c.z - b.z) * (px - b.x);
            const double d2 = (a.x - c.x) * (pz - c.z) - (a.z - c.z) * (px - c.x);
            const bool inside = (d0 >= 0.0 && d1 >= 0.0 && d2 >= 0.0) ||
                                (d0 <= 0.0 && d1 <= 0.0 && d2 <= 0.0);
            const double area = d0 + d1 + d2;
            if (inside && std::abs(area) > 1e-12) {
                out.push_back(t.originY + (d1 * a.y + d2 * b.y + d0 * c.y) / area);
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

int main() {
    const terrain::TerrainTiles tiles(terrain::kDefaultWorldSeed);
    std::vector<terrain::TileData> data;
    std::size_t openEdges = 0;
    for (std::int32_t z = -713'408; z <= -713'403; ++z) {
        for (std::int32_t x = 25'988; x <= 25'990; ++x) {
            data.push_back(tiles.generate({{{x, z}, 0}, 0}));
            openEdges += terrain::checkTileMesh(data.back().mesh,
                                                static_cast<float>(terrain::tileSizeMeters(0)))
                             .openEdges;
        }
    }
    const double heading = kHeading * std::numbers::pi / 180.0;
    const double dx = std::sin(heading);
    const double dz = -std::cos(heading);
    double floor = tiles.groundAt(kStartX, kStartZ);
    double steepest = 0.0;
    double lowestHeadroom = 1e9;
    double walked = 0.0;
    bool faced = false; // stopped by rock rising more than kStepMost ahead
    for (double t = kStep; t <= kMostWalk; t += kStep) {
        const std::vector<double> surfaces = surfacesAt(data, kStartX + dx * t, kStartZ + dz * t);
        double next = -1e9;
        double above = 1e9;
        for (const double s : surfaces) {
            next = s <= floor + kStepMost ? std::max(next, s) : next;
            above = s > floor + kStepMost ? std::min(above, s) : above;
        }
        if (next < -1e8) {
            faced = above < 1e8;
            break;
        }
        steepest = std::max(steepest, std::abs(next - floor));
        lowestHeadroom = std::min(lowestHeadroom, above - next);
        floor = next;
        walked = t;
    }
    std::printf("the walk to the foot of the K2: %zu open edges in its tiles; %.1f m walked to the "
                "face, steps of %.2f m at most every %.1f m, %.1f m of air over the walker at "
                "least\n",
                openEdges, walked, steepest, kStep, lowestHeadroom);
    TEST_CHECK(openEdges == 0);
    TEST_CHECK(faced && walked >= kLeastWalk && walked < kMostWalk);
    TEST_CHECK(steepest <= kStepMost);
    TEST_CHECK(lowestHeadroom >= kHeadroom);
    return test::result();
}

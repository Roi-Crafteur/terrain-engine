// Finite regions across an edge of the wrapped world (WorldSize): a region is a window in
// unwrapped coordinates. Centered on an edge or on the corner of the world, it reads the periodic
// world on both sides and is the very same window as at the other image of its center, bit for
// bit: grids, rivers, cascades, cliffs, cirques, arches and caves, its own objects moved by a
// whole world. Every query answers at the image of its point nearest to the center, so the
// region is seen as well from the canonical coordinates. A window wider than the world is
// refused.
#include "terrain/generation/cave_region.hpp"
#include "terrain/generation/eroded_region.hpp"
#include "terrain/generation/sculpted_region.hpp"

#include "legacy_relief_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {

using terrain::HeightmapRegion;
using terrain::WorldSize;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;
const WorldSize kWorld{};

// A mountainous stretch of the edge x = world.x / 2, at a southern mid-latitude, with an arch.
constexpr double kEdgeZ = -10282000.0;

struct Point {
    double x;
    double z;
};

// Points of the window of a region around its center, some of them beyond the edges of the
// world, on a lattice of 7.25 m (exact coordinates).
std::vector<Point> pointsOf(const HeightmapRegion& region) {
    std::vector<Point> points;
    const double half = region.size * region.metersPerPixel / 2.0;
    for (double dz = -half; dz <= half; dz += 7.25 * 17.0) {
        for (double dx = -half; dx <= half; dx += 7.25 * 13.0) {
            points.push_back({region.centerX + dx, region.centerZ + dz});
        }
    }
    return points;
}

// The eroded region centered on the corner (world.x / 2, world.z / 2) and on its image
// (-world.x / 2, -world.z / 2): the same grids and rivers, and the same heights at every image
// of a point of the window, in the frame of either window or in canonical coordinates.
void testErodedAcrossTheCorner() {
    const HeightmapRegion a{kWorld.x / 2.0, kWorld.z / 2.0, 32.0, 64};
    const HeightmapRegion b{-kWorld.x / 2.0, -kWorld.z / 2.0, 32.0, 64};
    const terrain::ErodedRegion first(kSeed, a,
                                      test_relief::onLegacyRelief(terrain::ErodedRegionParams{}));
    const terrain::ErodedRegion second(kSeed, b,
                                       test_relief::onLegacyRelief(terrain::ErodedRegionParams{}));
    const terrain::RiverNetwork& rivers = first.rivers();
    const terrain::RiverNetwork& other = second.rivers();
    TEST_CHECK(first.rawGrid().heights == second.rawGrid().heights);
    TEST_CHECK(first.grid().heights == second.grid().heights);
    TEST_CHECK(rivers.receiver == other.receiver && rivers.catchment == other.catchment &&
               rivers.river == other.river && rivers.cascades == other.cascades);
    bool same = true;
    std::size_t eroded = 0;
    for (const Point& p : pointsOf(a)) {
        const float height = first.heightAt(p.x, p.z);
        const double x = p.x - kWorld.x;
        const double z = p.z - kWorld.z;
        same = same && height == second.heightAt(x, z) && height == first.heightAt(x, z) &&
               height == second.heightAt(p.x, p.z) &&
               height == first.heightAt(terrain::wrapCoordinate(p.x, kWorld.x),
                                        terrain::wrapCoordinate(p.z, kWorld.z));
        eroded += height != first.field().heightAt(p.x, p.z) ? 1 : 0;
    }
    std::size_t riverCells = 0;
    for (const std::uint8_t cell : rivers.river) {
        riverCells += cell;
    }
    std::printf("eroded corner: %zu river cells, %zu cascades, %zu points changed by the "
                "erosion, the same at every image\n",
                riverCells, rivers.cascades.size(), eroded);
    TEST_CHECK(same && eroded > 0);
}

// Cliffs and sculpted relief on the edge x = world.x / 2 and on its image: the same grids, the
// same cirques and arches moved by a whole world (their cells by a whole number of cells), the
// same densities at every image of a point.
void testSculptedAcrossTheEdge() {
    const HeightmapRegion a{kWorld.x / 2.0, kEdgeZ, 16.0, 64};
    const HeightmapRegion b{-kWorld.x / 2.0, kEdgeZ, 16.0, 64};
    const terrain::SculptedRegion first(
        kSeed, a, test_relief::onLegacyRelief(terrain::SculptedRegionParams{}));
    const terrain::SculptedRegion second(
        kSeed, b, test_relief::onLegacyRelief(terrain::SculptedRegionParams{}));
    const terrain::CliffRegion& cliffs = first.cliffs();
    const terrain::CliffRegion& otherCliffs = second.cliffs();
    TEST_CHECK(cliffs.mask() == otherCliffs.mask());
    TEST_CHECK(cliffs.surface().heights == otherCliffs.surface().heights);
    TEST_CHECK(cliffs.riverDistance() == otherCliffs.riverDistance());
    TEST_CHECK(first.surface().heights == second.surface().heights);
    TEST_CHECK(first.overhangWeight() == second.overhangWeight());
    bool cirques = cliffs.cirques().size() == otherCliffs.cirques().size();
    for (std::size_t n = 0; cirques && n < cliffs.cirques().size(); ++n) {
        terrain::Cirque moved = cliffs.cirques()[n];
        moved.x -= kWorld.x;
        cirques = moved == otherCliffs.cirques()[n];
    }
    const auto cells = static_cast<int>(first.archField().cellsX().count);
    bool arches = first.arches().size() == second.arches().size();
    for (std::size_t n = 0; arches && n < first.arches().size(); ++n) {
        terrain::Arch moved = first.arches()[n];
        moved.x -= kWorld.x;
        moved.cellX -= cells;
        arches = moved == second.arches()[n];
    }
    bool same = true;
    std::size_t cliffPoints = 0;
    for (const Point& p : pointsOf(a)) {
        const double x = p.x - kWorld.x;
        const float y = cliffs.surfaceAt(p.x, p.z) - 3.0f;
        const float density = first.densityAt(p.x, y, p.z);
        same = same && density == second.densityAt(x, y, p.z) &&
               density == first.densityAt(x, y, p.z) &&
               cliffs.densityAt(p.x, y, p.z) == otherCliffs.densityAt(x, y, p.z);
        cliffPoints += cliffs.columnAt(p.x, p.z).cliff > 0.0f ? 1 : 0;
    }
    std::printf("sculpted edge: %zu points under cliffs, %zu cirques, %zu arches, the same at "
                "every image\n",
                cliffPoints, cliffs.cirques().size(), first.arches().size());
    TEST_CHECK(cirques && arches && same && cliffPoints > 0);
}

// The caves of a region centered on the corner: a sampler over a box across the corner, in the
// frame of the window, gives the densities of the same box around the other image of the corner
// and of the region centered there, which read the ground of the window in the canonical frame.
void testCavesAcrossTheCorner() {
    const HeightmapRegion a{kWorld.x / 2.0, kWorld.z / 2.0, 32.0, 32};
    const HeightmapRegion b{-kWorld.x / 2.0, -kWorld.z / 2.0, 32.0, 32};
    const terrain::CaveRegion first(kSeed, a,
                                    test_relief::onLegacyRelief(terrain::CaveRegionParams{}));
    const terrain::CaveRegion second(kSeed, b,
                                     test_relief::onLegacyRelief(terrain::CaveRegionParams{}));
    float low = first.groundAt(kWorld.x / 2.0, kWorld.z / 2.0);
    float high = low;
    for (double dz = -24.0; dz <= 24.0; dz += 1.0) {
        for (double dx = -24.0; dx <= 24.0; dx += 1.0) {
            const float ground = first.groundAt(kWorld.x / 2.0 + dx, kWorld.z / 2.0 + dz);
            low = std::min(low, ground);
            high = std::max(high, ground);
        }
    }
    const auto boxAround = [low, high](double x, double z) {
        return terrain::CaveBox{x - 24.0, static_cast<double>(low) - 90.0, z - 24.0,
                                x + 24.0, static_cast<double>(high) + 4.0, z + 24.0};
    };
    terrain::CaveSampler inA = first.sampler(boxAround(kWorld.x / 2.0, kWorld.z / 2.0));
    terrain::CaveSampler inB = second.sampler(boxAround(-kWorld.x / 2.0, -kWorld.z / 2.0));
    terrain::CaveSampler canonical = first.sampler(boxAround(-kWorld.x / 2.0, -kWorld.z / 2.0));
    bool same = true;
    std::size_t carved = 0;
    for (double dz = -23.5; dz <= 23.5; dz += 3.0) {
        for (double dx = -23.5; dx <= 23.5; dx += 3.0) {
            const double x = kWorld.x / 2.0 + dx;
            const double z = kWorld.z / 2.0 + dz;
            const float ground = first.groundAt(x, z);
            const float river = first.riverDistanceAt(x, z);
            same = same && ground == second.groundAt(x - kWorld.x, z - kWorld.z) &&
                   ground == first.groundAt(x - kWorld.x, z - kWorld.z) &&
                   river == second.riverDistanceAt(x - kWorld.x, z - kWorld.z) &&
                   river == first.riverDistanceAt(x - kWorld.x, z - kWorld.z);
            const terrain::CaveColumn a1 = inA.column(x, z, ground);
            const terrain::CaveColumn b1 = inB.column(x - kWorld.x, z - kWorld.z, ground);
            const terrain::CaveColumn c1 = canonical.column(x - kWorld.x, z - kWorld.z, ground);
            for (double depth = 2.0; depth <= 80.0; depth += 6.5) {
                const double y = static_cast<double>(ground) - depth;
                const auto rock = static_cast<float>(-depth);
                const float density = inA.density(a1, y, rock);
                same = same && density == inB.density(b1, y, rock) &&
                       density == canonical.density(c1, y, rock);
                carved += density != rock ? 1 : 0;
            }
        }
    }
    std::printf("caves at the corner: %zu samples touched by the caves, the same at every "
                "image\n",
                carved);
    TEST_CHECK(same);
}

template <typename Region>
bool refused(const HeightmapRegion& region, const WorldSize& world) {
    try {
        const Region built(kSeed, region, {}, world);
        (void)built;
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

// A window wider than the world along an axis is refused, by every kind of region; a window as
// wide as the world is accepted.
void testWiderThanTheWorld() {
    const WorldSize small{65536.0, 3.0 * 65536.0};
    const HeightmapRegion exact{0.0, 0.0, 1024.0, 64};
    const HeightmapRegion wider{0.0, 0.0, 1025.0, 64};
    const HeightmapRegion tall{0.0, 0.0, 40000.0, 1024}; // 40,960 km > 34,996 km along z
    TEST_CHECK(!refused<terrain::ErodedRegion>(exact, small));
    TEST_CHECK(refused<terrain::ErodedRegion>(wider, small));
    TEST_CHECK(refused<terrain::ErodedRegion>(tall, kWorld));
    TEST_CHECK(refused<terrain::CliffRegion>(wider, small));
    TEST_CHECK(refused<terrain::SculptedRegion>(wider, small));
    TEST_CHECK(refused<terrain::CaveRegion>(wider, small));
}

} // namespace

int main() {
    testErodedAcrossTheCorner();
    testSculptedAcrossTheEdge();
    testCavesAcrossTheCorner();
    testWiderThanTheWorld();
    return test::result();
}

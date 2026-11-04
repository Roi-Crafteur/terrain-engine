// The plates under the seas (agent 51, sea_plates.hpp): rebuilt from the stages of the geography,
// they are its own (the belts of its map to the bit); the ridges seed the distances through the
// sea, which grow by at most a step of the map from node to node; the frame of a ridge measures
// the distance to its axis as the plates of the settlement do; the same fields whatever the
// threads; the time and the bytes on the default world.

#include "sea_plates.hpp"
#include "settlement/geology_plates.hpp"

#include "dunes_support.hpp"
#include "test_assert.hpp"

#include "terrain/generation/geography.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>

namespace {

namespace sp = terrain::sea_plates;

void testGuard(const sp::Plates& p, const terrain::Geography& geo, const char* name) {
    std::size_t differ = 0;
    std::size_t labeled = 0;
    std::size_t sea = 0;
    for (int j = 0; j < p.grid.nz; ++j) {
        for (int i = 0; i < p.grid.nx; ++i) {
            const std::size_t k = p.grid.index(i, j);
            differ += p.belt[k] == geo.node(i, j, terrain::Geography::kBelt) ? 0 : 1;
            const bool atSea = geo.node(i, j, terrain::Geography::kCoast) <= 0.0f;
            sea += atSea ? 1 : 0;
            labeled += atSea && p.label[k] >= 0 ? 1 : 0;
        }
    }
    std::printf("%s: %zu ridges, %zu of %zu nodes at sea reached through the sea, %zu belts off "
                "the map\n",
                name, p.ridges.size(), labeled, sea, differ);
    TEST_CHECK(differ == 0);
    TEST_CHECK(!p.ridges.empty() && labeled > sea / 2);
}

// From node to node the distance through the sea grows by one step of the map at most.
void testDistances(const sp::Plates& p) {
    std::size_t bad = 0;
    const double step = std::hypot(p.grid.sizeX, p.grid.sizeZ) * 1.0001;
    for (int j = 0; j < p.grid.nz; ++j) {
        for (int i = 0; i < p.grid.nx; ++i) {
            const std::size_t k = p.grid.index(i, j);
            const std::size_t right = p.grid.index(i + 1, j);
            if (p.label[k] < 0 || p.label[right] < 0) {
                continue;
            }
            bad += std::abs(static_cast<double>(p.distance[k] - p.distance[right])) > step ? 1 : 0;
        }
    }
    TEST_CHECK(bad == 0);
}

// Near the axes, the frame of the nearest ridge gives the distance of the plates of the settlement.
void testFrames(const sp::Plates& p, const terrain::settlement::GeographyPlates& gp) {
    std::size_t checked = 0;
    double worst = 0.0;
    for (int j = 0; j < p.grid.nz; ++j) {
        for (int i = 0; i < p.grid.nx; ++i) {
            const std::size_t k = p.grid.index(i, j);
            if (p.label[k] < 0 || p.distance[k] > 50'000.0f) {
                continue;
            }
            const double x = terrain::wrapCoordinate(p.grid.x(i) - p.anchorX + 777.0, p.world.x);
            const double z = terrain::wrapCoordinate(p.grid.z(j) + 333.0, p.world.z);
            const terrain::settlement::GeographyPlates::Near near = gp.nearAt(x, z);
            const sp::Ridge& r = p.ridges[static_cast<std::size_t>(p.label[k])];
            if (near.boundary != r.boundary) {
                continue;
            }
            const sp::Frame f = sp::frameOf(r, sp::warpedAt(p, x, z), p.world);
            worst = std::max(worst, std::abs(std::abs(f.v) - std::abs(near.towardB)));
            ++checked;
        }
    }
    std::printf(
        "frames: %zu points near the axes, the distance to the axis off by %.3f m at most\n",
        checked, worst);
    TEST_CHECK(checked >= 5 && worst < 1.0);
}

void testSmallWorld() {
    const terrain::Heightfield& relief = dunes_test::small().relief;
    const terrain::Geography& geo = *relief.geography();
    const sp::Plates one = sp::build(dunes_test::kSeed, geo, 1);
    const sp::Plates four = sp::build(dunes_test::kSeed, geo, 4);
    TEST_CHECK(one.offsetX == four.offsetX && one.label == four.label &&
               one.distance == four.distance && one.trench == four.trench &&
               one.active == four.active && one.ridges.size() == four.ridges.size());
    testGuard(one, geo, "small world");
    testDistances(one);
    const terrain::settlement::GeographyPlates gp(dunes_test::kSeed, geo, 4);
    testFrames(one, gp);
}

void testDefaultWorld() {
    const terrain::Heightfield relief(dunes_test::kSeed);
    const auto start = std::chrono::steady_clock::now();
    const sp::Plates p = sp::build(dunes_test::kSeed, *relief.geography(), 0);
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("default world: built in %.2f s, %.1f MB\n", seconds,
                static_cast<double>(p.memoryBytes()) / (1024.0 * 1024.0));
    TEST_CHECK(p.memoryBytes() < 32u * 1024u * 1024u);
    testGuard(p, *relief.geography(), "default world");
    testDistances(p);
    const terrain::settlement::GeographyPlates gp(dunes_test::kSeed, *relief.geography(), 0);
    testFrames(p, gp);
}

} // namespace

int main() {
    testSmallWorld();
    testDefaultWorld();
    return test::result();
}

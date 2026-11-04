// The links of the planet level through the passes (planet_links.hpp, task 11C step 1): the
// barrier of a way between two floors, the receivers that prefer an open way, and on the small
// world fewer rivers crossing a crest, the same bits on every thread.
#include "terrain/generation/planet/planet_links.hpp"
#include "terrain/generation/planet/planet_order.hpp"
#include "terrain/generation/planet/planet_relief.hpp"

#include "planet_golden_support.hpp"

#include "test_assert.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <vector>

namespace {

using terrain::PlanetGrid;
using terrain::planet::LinkLadder;

// Two valleys along z, at x = -4 km and x = +4 km, down to the south at 2 %, a crest of 600 m
// between them along x = 0.
float valleys(double x, double z) {
    const double crest = 600.0 * std::exp(-(x / 2000.0) * (x / 2000.0));
    return static_cast<float>(1000.0 - 0.02 * z + crest);
}

const terrain::planet::LinkHeights kValleys = [](std::span<const double> xs,
                                                 std::span<const double> zs,
                                                 std::span<float> out) {
    for (std::size_t i = 0; i < xs.size(); ++i) {
        out[i] = valleys(xs[i], zs[i]);
    }
};

void testBarrier() {
    const LinkLadder params;
    // Down the valley: the way never climbs.
    const float down = terrain::planet::linkBarrier(kValleys, -4000.0, 0.0, -4000.0, 8000.0, params);
    TEST_CHECK(down <= valleys(-4000.0, 0.0));
    // Across the crest, even down to a lower floor: the crest stands in the way.
    const float across =
        terrain::planet::linkBarrier(kValleys, -4000.0, 0.0, 4000.0, 6000.0, params);
    std::printf("barriers: %.1f m down the valley, %.1f m across the crest (floor %.1f m)\n", down,
                across, valleys(-4000.0, 0.0));
    TEST_CHECK(across > valleys(-4000.0, 0.0) + 400.0);
    // A pass in the crest within the lanes: the way finds it.
    const terrain::planet::LinkHeights pass = [](std::span<const double> xs,
                                                 std::span<const double> zs, std::span<float> out) {
        for (std::size_t i = 0; i < xs.size(); ++i) {
            const double notch = std::abs(zs[i] - 3000.0) < 800.0 ? 0.0 : 1.0;
            out[i] = static_cast<float>(1000.0 - 0.02 * zs[i] +
                                        600.0 * notch * std::exp(-(xs[i] / 2000.0) *
                                                                 (xs[i] / 2000.0)));
        }
    };
    const float through = terrain::planet::linkBarrier(pass, -4000.0, 0.0, 4000.0, 6000.0, params);
    TEST_CHECK(through < across - 300.0);
}

// The exits of a cell of 3 x 3 points whose floor lies west of a crest along its east column:
// low to the west, high to the east, over the crest to the north-east (steps of 8 m, rounded up).
void testExits() {
    const float heights[9] = {300.0f, 320.0f, 900.0f, //
                              100.0f, 250.0f, 950.0f, //
                              280.0f, 300.0f, 920.0f};
    const terrain::planet::CellExits e = terrain::planet::cellExits(7, heights, 3, 3);
    TEST_CHECK(e.column == 7 && e.floor == 100);
    TEST_CHECK(e.exit(1) == 100.0); // west: the floor lies on the west side
    TEST_CHECK(e.exit(0) == 900.0); // east: over the crest at its lowest
    TEST_CHECK(e.exit(2) == 284.0); // south: the lowest point of the south row, 280, rounded up
    TEST_CHECK(e.exit(3) == 300.0); // north
    TEST_CHECK(e.exit(4) == 924.0); // south-east corner, 920
}

// A grid of 3 x 3 cells: the middle one has two neighbors below it, the steepest across a crest.
void testReceivers() {
    const PlanetGrid grid{3, 3, 8192.0};
    std::vector<float> height(grid.cells(), 500.0f);
    height[grid.index(1, 1)] = 400.0f;
    height[grid.index(2, 1)] = 100.0f; // east, steepest
    height[grid.index(1, 2)] = 300.0f; // south, gentler
    std::vector<std::uint8_t> ocean(grid.cells(), terrain::planet::kLand);
    const auto land = std::make_shared<const terrain::planet::LandIndex>(ocean);
    const std::size_t middle = grid.index(1, 1);
    const auto east = static_cast<std::uint8_t>(0); // kPlanetNeighbors: +x first
    const auto south = static_cast<std::uint8_t>(2);
    const auto plain = terrain::planet::computeReceivers(grid, height, land, 1);
    TEST_CHECK(plain[middle] == east);
    terrain::planet::ReceiverLinks links;
    links.closed = [](std::size_t) { return std::uint8_t{1u << 0}; }; // a crest to the east
    const auto through = terrain::planet::computeReceivers(grid, height, land, 1, &links);
    TEST_CHECK(through[middle] == south);
    // Nothing open: the steepest, as before.
    links.closed = [](std::size_t) { return std::uint8_t{0xFF}; };
    TEST_CHECK(terrain::planet::computeReceivers(grid, height, land, 1, &links)[middle] == east);
    // Nothing closed: the steepest.
    links.closed = [](std::size_t) { return std::uint8_t{0}; };
    TEST_CHECK(terrain::planet::computeReceivers(grid, height, land, 1, &links)[middle] == east);
    // The mask of the exits: a crest between the middle and its east neighbor closes that link.
    std::vector<std::vector<terrain::planet::CellExits>> exits(3);
    terrain::planet::CellExits middleExits;
    middleExits.column = 1;
    middleExits.floor = 400;
    middleExits.climb.fill(1);  // 408 m
    middleExits.climb[0] = 75;  // 1000 m to the east
    terrain::planet::CellExits southExits;
    southExits.column = 1;
    southExits.floor = 300;
    southExits.climb.fill(25); // 500 m: its way up north climbs over 400 + 40 m
    exits[1].push_back(middleExits);
    exits[2].push_back(southExits);
    const auto closed = terrain::planet::closedLinks(grid, exits, land, terrain::PlanetLinkParams{}, 1);
    TEST_CHECK((closed[middle] & 1u) != 0);        // east
    TEST_CHECK((closed[middle] & (1u << 2)) != 0); // south: the neighbor's side climbs
    TEST_CHECK((closed[middle] & (1u << 1)) == 0); // west: 8 m above the floor, open
    TEST_CHECK(closed[grid.index(0, 0)] == 0);     // no exits, nothing closed
}

// How many links of the rivers of a build climb more than 100 m above their floors on a fine
// ladder (sections every 250 m, lanes 500 m apart over 4 km): rivers crossing a crest.
int crestCrossings(const terrain::Heightfield& relief, const terrain::PlanetBuild& build,
                   int floorSamples) {
    LinkLadder fine;
    fine.sections = 32;
    fine.lanes = 9;
    fine.laneSpacing = 500.0;
    int crossings = 0;
    for (const terrain::RiverSegment& segment : build.rivers.segments) {
        for (std::size_t k = 0; k + 1 < segment.vertices.size(); ++k) {
            const std::uint32_t a = segment.vertices[k].cell;
            const std::uint32_t b = segment.vertices[k + 1].cell;
            if (build.mountain[a] == 0 || build.ocean[b] != terrain::planet::kLand) {
                continue;
            }
            const auto pa = terrain::planet::floorPoint(build.grid, a, build.floor[a], floorSamples);
            const auto pb = terrain::planet::floorPoint(build.grid, b, build.floor[b], floorSamples);
            const double bx = terrain::nearestImage(pb[0], pa[0], build.grid.periodX());
            const double bz = terrain::nearestImage(pb[1], pa[1], build.grid.periodZ());
            const float barrier =
                terrain::planet::linkBarrier(relief, pa[0], pa[1], bx, bz, fine);
            crossings += barrier > std::max(build.height[a], build.height[b]) + 100.0f ? 1 : 0;
        }
    }
    return crossings;
}

void testSmallWorld() {
    const terrain::Heightfield field(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                                     golden::kSmallPlanet);
    terrain::PlanetParams on = golden::smallPlanetParams(1, 1);
    terrain::PlanetParams off = on;
    off.links.enabled = false;
    terrain::PlanetParams four = on;
    four.threads = 4;
    const terrain::PlanetBuild with = terrain::planet::buildPlanet(field, on);
    const terrain::PlanetBuild without = terrain::planet::buildPlanet(field, off);
    const terrain::PlanetBuild threads = terrain::planet::buildPlanet(field, four);
    const int before = crestCrossings(field, without, on.floorSamples);
    const int after = crestCrossings(field, with, on.floorSamples);
    std::printf("links of rivers crossing a crest on the small world: %d without the passes, %d "
                "with them\n",
                before, after);
    TEST_CHECK(after < before);
    TEST_CHECK(with.receiver == threads.receiver);
    TEST_CHECK(with.discharge == threads.discharge);
}

} // namespace

int main() {
    testBarrier();
    testExits();
    testReceivers();
    testSmallWorld();
    return test::result();
}

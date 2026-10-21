// Figures of the planet level: land masses, basins, the longest rivers (through the lakes).
#include "terrain/generation/planet/planet_stats.hpp"

#include "planet_golden_support.hpp"
#include "test_assert.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

using terrain::RiverGraph;
using terrain::RiverMouth;
using terrain::RiverSegment;

void testLandMasses() {
    const terrain::PlanetGrid grid{10, 6, 1000.0};
    std::vector<std::uint8_t> ocean(grid.cells(), 1);
    // Across the east-west seam (3 cells), diagonal neighbors (2), across the polar line (2).
    for (const auto& [i, j] : {std::pair{0, 1}, {9, 1}, {9, 2}, {4, 3}, {5, 4}, {2, 5}, {2, 0}}) {
        ocean[grid.index(i, j)] = 0;
    }
    const std::vector<double> masses = terrain::planet::landMasses(grid, ocean);
    TEST_CHECK(masses == (std::vector<double>{3.0e6, 2.0e6, 2.0e6}));
}

RiverSegment segment(double length, RiverMouth mouth, float discharge) {
    RiverSegment s;
    s.length = length;
    s.mouth = mouth;
    s.vertices.resize(2);
    s.vertices[0].discharge = discharge;
    s.vertices[1].discharge = discharge;
    return s;
}

RiverSegment ending(double length, RiverMouth mouth, float discharge, double lastX) {
    RiverSegment s = segment(length, mouth, discharge);
    s.vertices[1].x = lastX;
    s.vertices[1].z = -1.0; // the centers of row 0
    return s;
}

terrain::PlanetLake lake(std::uint32_t partner, std::int64_t outflow) {
    terrain::PlanetLake l;
    l.overflowing = true;
    l.pass = partner - 1;
    l.partner = partner;
    l.outflow = outflow;
    return l;
}

void testLongestRivers() {
    // On rows of cells 1 m wide (cell i of row 0 centered on (i - 99.5, -1)): two sources (100 and
    // 300 m) join (50 m) and enter lake 0 at x = -50.5, which spills at cell 60 (x = -39.5,
    // 11 m across) into a river to the sea (200 m): 561 m, 11 of them across the lake. A lone
    // river of 400 m to the sea. A river of 10 m enters lake 1 at x = 0.5, which spills at cell
    // 110 straight into lake 2, which spills at cell 130 into the sea: 10 + 10 + 20 = 40 m, 30 of
    // them across the lakes, with the outflow of lake 2.
    const terrain::PlanetGrid grid{200, 3, 1.0};
    RiverGraph graph;
    graph.segments.push_back(segment(100.0, RiverMouth::Confluence, 1.0f));
    graph.segments.push_back(segment(300.0, RiverMouth::Confluence, 2.0f));
    graph.segments.push_back(ending(50.0, RiverMouth::Lake, 3.0f, -50.5));
    graph.segments.push_back(segment(200.0, RiverMouth::Sea, 4.0f));
    graph.segments.push_back(segment(400.0, RiverMouth::Sea, 5.0f));
    graph.segments.push_back(ending(10.0, RiverMouth::Lake, 6.0f, 0.5));
    graph.segments[0].downstream = 2;
    graph.segments[1].downstream = 2;
    graph.segments[2].upstream = {0, 1};
    graph.segments[2].mouthLake = 0;
    graph.segments[3].sourceLakes = {0};
    graph.segments[5].mouthLake = 1;
    graph.lakeStrahler = {2, 1, 1};
    const std::vector<terrain::PlanetLake> lakes = {lake(60, 5'000'000), lake(110, 3'000'000),
                                                    lake(130, 4'000'000)};
    const auto lakeAt = [](std::size_t c) { return c == 110 ? 2 : -1; };
    const auto isOcean = [](std::size_t c) { return c == 130; };
    const std::vector<terrain::PlanetRiverStat> rivers =
        terrain::planet::longestRivers(graph, lakes, grid, lakeAt, isOcean, 1.0, 5);
    TEST_CHECK(rivers.size() == 3);
    if (rivers.size() == 3) {
        TEST_CHECK(rivers[0].mouthSegment == 3 && rivers[0].length == 561.0);
        TEST_CHECK(rivers[0].lakeLength == 11.0);
        TEST_CHECK(rivers[0].discharge == 4.0 && rivers[0].mouth == RiverMouth::Sea);
        TEST_CHECK(rivers[1].mouthSegment == 4 && rivers[1].length == 400.0);
        TEST_CHECK(rivers[1].lakeLength == 0.0);
        TEST_CHECK(rivers[2].mouthSegment == -1 && rivers[2].length == 40.0);
        TEST_CHECK(rivers[2].lakeLength == 30.0);
        TEST_CHECK(rivers[2].discharge == 4.0 && rivers[2].mouth == RiverMouth::Sea);
        TEST_CHECK(rivers[2].x == grid.centerX(130));
    }
    TEST_CHECK(terrain::planet::longestRivers(graph, lakes, grid, lakeAt, isOcean, 1.0, 1).size() ==
               1);
    // Lake 1 lets out 3 m^3/s, less than a river of 4.5 m^3/s: the river of 10 m ends in it.
    const std::vector<terrain::PlanetRiverStat> larger =
        terrain::planet::longestRivers(graph, lakes, grid, lakeAt, isOcean, 4.5, 5);
    TEST_CHECK(larger.size() == 3);
    if (larger.size() == 3) {
        TEST_CHECK(larger[0].length == 561.0 && larger[1].length == 400.0);
        TEST_CHECK(larger[2].mouthSegment == 5 && larger[2].length == 10.0);
        TEST_CHECK(larger[2].lakeLength == 0.0 && larger[2].discharge == 6.0);
        TEST_CHECK(larger[2].mouth == RiverMouth::Lake);
    }
}

void testSmallWorld() {
    const terrain::PlanetLevel level(terrain::kDefaultWorldSeed, golden::smallPlanetaryParams(),
                                     golden::smallPlanetParams(2), golden::kSmallPlanet);
    const terrain::PlanetStats stats = terrain::planet::planetStats(level, 5);
    const double total = static_cast<double>(level.grid().cells()) * level.grid().cellArea();
    double land = 0.0;
    for (const double mass : stats.landMasses) {
        land += mass;
    }
    TEST_CHECK(stats.landShare > 0.3 && stats.landShare < 0.9);
    TEST_CHECK(std::abs(land - stats.landShare * total) < 1.0);
    TEST_CHECK(stats.largestBasins.size() == 5 && stats.longestRivers.size() >= 1);
    for (std::size_t k = 1; k < stats.largestBasins.size(); ++k) {
        TEST_CHECK(stats.largestBasins[k].area <= stats.largestBasins[k - 1].area);
    }
    TEST_CHECK(stats.largestBasins[0].area <= stats.landMasses[0]);
    for (std::size_t k = 1; k < stats.longestRivers.size(); ++k) {
        TEST_CHECK(stats.longestRivers[k].length <= stats.longestRivers[k - 1].length);
    }
    for (const terrain::PlanetRiverStat& river : stats.longestRivers) {
        TEST_CHECK(river.lakeLength >= 0.0 && river.lakeLength < river.length);
    }
    TEST_CHECK(stats.lakes == level.lakes().size() &&
               stats.lakes == stats.overflowingLakes + stats.terminalLakes + stats.playas);
    TEST_CHECK(stats.lakeArea ==
               static_cast<double>(level.cells().lakeCells.size()) * level.grid().cellArea());
}

} // namespace

int main() {
    testLandMasses();
    testLongestRivers();
    testSmallWorld();
    return test::result();
}

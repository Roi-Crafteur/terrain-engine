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

void testLongestRivers() {
    // Two sources (100 and 300 m) join, enter lake 0, which spills into a river to the sea
    // (200 m): 550 m. A lone river of 400 m to the sea; one of 10 m into lake 1, which has no
    // outflow river: it ends there.
    RiverGraph graph;
    graph.segments.push_back(segment(100.0, RiverMouth::Confluence, 1.0f));
    graph.segments.push_back(segment(300.0, RiverMouth::Confluence, 2.0f));
    graph.segments.push_back(segment(50.0, RiverMouth::Lake, 3.0f));
    graph.segments.push_back(segment(200.0, RiverMouth::Sea, 4.0f));
    graph.segments.push_back(segment(400.0, RiverMouth::Sea, 5.0f));
    graph.segments.push_back(segment(10.0, RiverMouth::Lake, 6.0f));
    graph.segments[0].downstream = 2;
    graph.segments[1].downstream = 2;
    graph.segments[2].upstream = {0, 1};
    graph.segments[2].mouthLake = 0;
    graph.segments[3].sourceLakes = {0};
    graph.segments[5].mouthLake = 1;
    graph.lakeStrahler = {2, 1};
    const std::vector<terrain::PlanetRiverStat> rivers = terrain::planet::longestRivers(graph, 5);
    TEST_CHECK(rivers.size() == 3);
    if (rivers.size() == 3) {
        TEST_CHECK(rivers[0].mouthSegment == 3 && rivers[0].length == 550.0);
        TEST_CHECK(rivers[0].discharge == 4.0 && rivers[0].mouth == RiverMouth::Sea);
        TEST_CHECK(rivers[1].mouthSegment == 4 && rivers[1].length == 400.0);
        TEST_CHECK(rivers[2].mouthSegment == 5 && rivers[2].length == 10.0);
    }
    TEST_CHECK(terrain::planet::longestRivers(graph, 1).size() == 1);
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
    TEST_CHECK(stats.lakes == level.lakes().size() &&
               stats.lakes == stats.overflowingLakes + stats.terminalLakes + stats.playas);
    TEST_CHECK(stats.riverLength >= stats.longestRivers[0].length);
}

} // namespace

int main() {
    testLandMasses();
    testLongestRivers();
    testSmallWorld();
    return test::result();
}

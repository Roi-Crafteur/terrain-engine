// River graph of the planet level: segments, confluences, Strahler orders (through lakes too),
// discharge, polylines across the seam, mouths and ice.
#include "planet_test_support.hpp"
#include "test_assert.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using planet_test::Network;
using planet_test::network;
using planet_test::plateau;
using planet_test::sources;
using planet_test::Terrain;
using terrain::RiverGraph;
using terrain::RiverMouth;
using terrain::RiverSegment;
using terrain::planet::CellWater;

float units(std::int64_t u) {
    return static_cast<float>(terrain::planet::cubicMeters(u));
}

void testStrahler() {
    // A main stem along row 5 (10 m per column) with tributary A at column 15 and B at column
    // 10 (order 1 each), and C (order 2: C1 from (7, 1), C2 from (3, 1)) meeting it at column 5.
    const Terrain t = plateau(20, 11,
                              {{15, 1, 400},
                               {15, 2, 350},
                               {15, 3, 300},
                               {15, 4, 250},
                               {10, 9, 400},
                               {10, 8, 350},
                               {10, 7, 300},
                               {10, 6, 250},
                               {7, 1, 500},
                               {6, 2, 450},
                               {3, 1, 500},
                               {4, 2, 450},
                               {5, 3, 400},
                               {5, 4, 300}});
    Terrain main = t;
    for (int i = 1; i <= 18; ++i) {
        main.at(i, 5) = 10.0f * static_cast<float>(i);
    }
    main.markOcean();
    const Network n = network(main, sources(main, {{18, 5}, {15, 1}, {10, 9}, {7, 1}, {3, 1}}));
    const std::vector<RiverSegment>& s = n.graph.segments;
    TEST_CHECK(s.size() == 9);
    if (s.size() != 9) {
        return;
    }
    // Segments by head cell: C2, C1, A, C, lower stem, middle stem, upper stem, source, B.
    const int strahler[] = {1, 1, 1, 2, 3, 2, 2, 1, 1};
    const int downstream[] = {3, 3, 6, 4, -1, 4, 5, 6, 5};
    for (std::size_t k = 0; k < 9; ++k) {
        TEST_CHECK(s[k].strahler == strahler[k] && s[k].downstream == downstream[k]);
    }
    TEST_CHECK(s[4].upstream == (std::vector<std::int32_t>{3, 5}));
    TEST_CHECK(s[5].upstream == (std::vector<std::int32_t>{6, 8}));
    TEST_CHECK(s[6].upstream == (std::vector<std::int32_t>{2, 7}));
    TEST_CHECK(s[3].upstream == (std::vector<std::int32_t>{0, 1}));
    // The lower stem reaches the sea at (0, 5), sea level.
    TEST_CHECK(s[4].mouth == RiverMouth::Sea && s[4].vertices.size() == 6);
    TEST_CHECK(s[4].vertices.back().cell == main.cell(0, 5) &&
               s[4].vertices.back().elevation == 0.0f);
    TEST_CHECK(s[2].vertices.back().cell == main.cell(15, 5)); // A ends at the confluence
    // Discharge adds up at the confluences.
    TEST_CHECK(n.flow.discharge[main.cell(16, 5)] == units(1000));
    TEST_CHECK(n.flow.discharge[main.cell(15, 5)] == units(2000));
    TEST_CHECK(n.flow.discharge[main.cell(10, 5)] == units(3000));
    TEST_CHECK(n.flow.discharge[main.cell(5, 5)] == units(5000));
    TEST_CHECK(s[4].vertices[0].discharge == units(5000));
    TEST_CHECK(s[4].vertices[0].width ==
               static_cast<float>(7.0 * std::sqrt(static_cast<double>(units(5000)))));
    TEST_CHECK(std::abs(s[4].length - 5.0 * main.grid.cellSize) < 1e-9);
}

void testThroughLake() {
    // Two rivers from the north and the south fill a lake at columns 8 to 10, which spills at
    // (7, 5) into a channel down to the sea: its outflow is of order 2.
    Terrain t = plateau(20, 11,
                        {{9, 1, 500},
                         {9, 2, 450},
                         {9, 3, 400},
                         {9, 4, 300},
                         {9, 9, 500},
                         {9, 8, 450},
                         {9, 7, 400},
                         {9, 6, 300},
                         {8, 5, 100},
                         {9, 5, 90},
                         {10, 5, 100},
                         {7, 5, 150}});
    for (int i = 1; i <= 6; ++i) {
        t.at(i, 5) = 10.0f * static_cast<float>(i);
    }
    t.markOcean();
    const Network n = network(t, sources(t, {{9, 1}, {9, 9}}));
    TEST_CHECK(n.lakes.lakes.size() == 1 && n.lakes.lakes[0].overflowing);
    const std::vector<RiverSegment>& s = n.graph.segments;
    TEST_CHECK(s.size() == 3);
    if (s.size() != 3) {
        return;
    }
    TEST_CHECK(s[0].mouth == RiverMouth::Lake && s[0].mouthLake == 0 && s[0].strahler == 1);
    TEST_CHECK(s[2].mouth == RiverMouth::Lake && s[2].mouthLake == 0 && s[2].strahler == 1);
    TEST_CHECK(s[1].sourceLakes == (std::vector<std::int32_t>{0}) && s[1].strahler == 2);
    TEST_CHECK(s[1].vertices[0].cell == t.cell(6, 5) && s[1].mouth == RiverMouth::Sea);
    TEST_CHECK(n.graph.lakeStrahler[0] == 2);
    TEST_CHECK(s[0].vertices.back().elevation == n.lakes.lakes[0].level);
    TEST_CHECK(n.flow.discharge[t.cell(6, 5)] == units(2000));
}

// A valley along row 2 from (15, 2) east across the seam to the sea at column 5, a wall at
// column 14.
Terrain seamValley() {
    Terrain t = planet_test::flatTerrain(20, 5, 0.0f);
    for (int j = 0; j < 5; ++j) {
        const float side = 1000.0f * static_cast<float>(j < 2 ? 2 - j : j - 2);
        for (int i = 0; i < 20; ++i) {
            const int toSea = (5 - i + 20) % 20; // cells to the sea going east
            t.at(i, j) = i > 5 && i < 14 ? 10.0f * static_cast<float>(i - 5) + side
                                         : 10.0f * static_cast<float>(toSea) + side;
        }
        t.at(5, j) = -100.0f;
        t.at(14, j) = 5000.0f;
    }
    t.markOcean();
    return t;
}

void testAcrossSeam() {
    const Terrain t = seamValley();
    const Network n = network(t, sources(t, {{15, 2}}));
    TEST_CHECK(n.graph.segments.size() == 1);
    if (n.graph.segments.empty()) {
        return;
    }
    const RiverSegment& river = n.graph.segments[0];
    TEST_CHECK(river.mouth == RiverMouth::Sea && river.vertices.size() == 11);
    bool continuous = true;
    for (std::size_t v = 1; v < river.vertices.size(); ++v) {
        continuous = continuous &&
                     river.vertices[v].x - river.vertices[v - 1].x == t.grid.cellSize &&
                     river.vertices[v].z == river.vertices[0].z;
    }
    TEST_CHECK(continuous && river.vertices[5].cell == t.cell(0, 2));
    TEST_CHECK(river.vertices.back().x > 0.5 * t.grid.periodX()); // past the seam, unwrapped
    TEST_CHECK(std::abs(river.length - 10.0 * t.grid.cellSize) < 1e-9);
}

void testIce() {
    // The same valley frozen over columns 0 to 2: the river ends on the ice, and comes out of it
    // again below.
    const Terrain t = seamValley();
    std::vector<CellWater> table = sources(t, {{15, 2}});
    for (int j = 0; j < 5; ++j) {
        for (int i = 0; i <= 2; ++i) {
            table[t.cell(i, j)].frozen = true;
        }
    }
    const Network n = network(t, table);
    TEST_CHECK(n.graph.segments.size() == 2);
    if (n.graph.segments.size() != 2) {
        return;
    }
    const RiverSegment& below = n.graph.segments[0]; // head (3, 2) comes first
    const RiverSegment& above = n.graph.segments[1];
    TEST_CHECK(above.mouth == RiverMouth::Ice && above.vertices.back().cell == t.cell(0, 2));
    TEST_CHECK(below.vertices[0].cell == t.cell(3, 2) && below.mouth == RiverMouth::Sea);
    TEST_CHECK(below.strahler == 1 && below.upstream.empty());
    TEST_CHECK(below.vertices[0].discharge == units(1000));
}

void testConsistency() {
    Terrain t = planet_test::noisyTerrain(96, 48, 17, -1500.0f, 2500.0f, 2);
    t.grid.cellSize = 50000.0;
    t.markOcean(1.0e10);
    const terrain::PlanetClimateParams params;
    const auto climate = terrain::planet::computeClimate(t.grid, t.height, t.ocean, params, 1);
    std::vector<CellWater> table(t.grid.cells());
    const auto water = terrain::planet::climateWater(t.grid, t.height, t.ocean, climate, params);
    for (std::size_t c = 0; c < t.grid.cells(); ++c) {
        table[c] = water(c);
    }
    const Network n = network(t, table);
    TEST_CHECK(n.graph.segments.size() > 10);
    bool ok = true;
    for (const RiverSegment& s : n.graph.segments) {
        ok = ok && s.vertices.size() >= 2;
        for (std::size_t v = 1; v + 1 < s.vertices.size(); ++v) {
            ok = ok && s.vertices[v].discharge >= s.vertices[v - 1].discharge;
        }
        for (std::size_t v = 1; v < s.vertices.size(); ++v) {
            ok = ok && std::abs(s.vertices[v].x - s.vertices[v - 1].x) <= t.grid.cellSize &&
                 std::abs(s.vertices[v].z - s.vertices[v - 1].z) <= t.grid.cellSize;
        }
        const std::uint32_t last = s.vertices.back().cell;
        switch (s.mouth) {
        case RiverMouth::Confluence: {
            const RiverSegment& d = n.graph.segments[static_cast<std::size_t>(s.downstream)];
            ok = ok && d.vertices[0].cell == last && d.strahler >= s.strahler;
            break;
        }
        case RiverMouth::Sea:
            ok = ok && t.ocean[last] != terrain::planet::kLand;
            break;
        case RiverMouth::Lake:
        case RiverMouth::TerminalLake:
            ok = ok && n.lakes.lakeOfCell[last] == s.mouthLake &&
                 n.lakes.lakes[static_cast<std::size_t>(s.mouthLake)].overflowing ==
                     (s.mouth == RiverMouth::Lake);
            break;
        case RiverMouth::ClosedBasin:
            ok = ok &&
                 (s.mouthLake < 0 || n.lakes.lakes[static_cast<std::size_t>(s.mouthLake)].playa);
            break;
        case RiverMouth::Ice:
            ok = ok && table[last].frozen;
            break;
        }
    }
    TEST_CHECK(ok);
}

} // namespace

int main() {
    testStrahler();
    testThroughLake();
    testAcrossSeam();
    testIce();
    testConsistency();
    return test::result();
}

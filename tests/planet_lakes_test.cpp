// Lakes of the planet level: Fill-Spill-Merge in the steady state with evaporation. Terminal
// lakes, overflowing lakes, transfers between depressions and between trees, frozen basins and
// the exact conservation of the water.
#include "terrain/generation/planet/planet_lakes.hpp"
#include "terrain/generation/planet/planet_order.hpp"

#include "planet_test_support.hpp"
#include "test_assert.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using planet_test::Terrain;
using terrain::planet::CellWater;
using terrain::planet::LakeSolution;
using terrain::PlanetLake;

// Runoff and lake evaporation per cell, in water units.
struct Table {
    std::vector<std::int64_t> runoff;
    std::vector<std::int64_t> evaporation;
    std::vector<bool> frozen;

    explicit Table(const Terrain& t)
        : runoff(t.grid.cells(), 0), evaporation(t.grid.cells(), 0), frozen(t.grid.cells(), false) {
    }
};

struct Solved {
    terrain::planet::DepressionHierarchy hierarchy;
    LakeSolution lakes;
};

Solved solve(const Terrain& t, const Table& table) {
    const std::vector<std::uint32_t> order = terrain::planet::sortCells(t.height, 1);
    const auto receiver = terrain::planet::computeReceivers(t.grid, t.height, t.land, 1);
    Solved s;
    s.hierarchy = terrain::planet::buildHierarchy(t.grid, t.height, t.land, order, receiver);
    const terrain::planet::WaterOf water = [&](std::size_t c) {
        return CellWater{table.runoff[c], table.evaporation[c], table.frozen[c]};
    };
    s.lakes = terrain::planet::solveLakes(t.grid, t.height, s.hierarchy, order, water);
    return s;
}

std::int64_t evaporated(const LakeSolution& lakes) {
    std::int64_t sum = 0;
    for (const PlanetLake& lake : lakes.lakes) {
        sum += lake.evaporation;
        TEST_CHECK(lake.inflow == lake.evaporation + lake.outflow);
        TEST_CHECK(lake.overflowing ? lake.outflow > 0 : lake.outflow == 0);
    }
    return sum;
}

// An island of 50 m with one basin of 8 cells at 10 to 17 m, opening to the sea at (2, 3)
// (40 m).
Terrain oneBasinIsland() {
    Terrain t = planet_test::flatTerrain(10, 8, -100.0f);
    for (int j = 2; j <= 5; ++j) {
        for (int i = 2; i <= 7; ++i) {
            t.at(i, j) = 50.0f;
        }
    }
    float h = 10.0f;
    for (int j = 3; j <= 4; ++j) {
        for (int i = 3; i <= 6; ++i) {
            t.at(i, j) = h;
            h += 1.0f;
        }
    }
    t.at(2, 3) = 40.0f;
    t.markOcean();
    return t;
}

Table basinWater(const Terrain& t, std::int64_t runoff, std::int64_t evaporation) {
    Table table(t);
    for (int j = 3; j <= 4; ++j) {
        for (int i = 3; i <= 6; ++i) {
            table.runoff[t.cell(i, j)] = runoff;
            table.evaporation[t.cell(i, j)] = evaporation;
        }
    }
    return table;
}

void testTerminalLake() {
    const Terrain t = oneBasinIsland();
    const Solved s = solve(t, basinWater(t, 1000, 3000));
    TEST_CHECK(s.lakes.lakes.size() == 1);
    const PlanetLake& lake = s.lakes.lakes[0];
    // 8,000 units evaporate over 2 cells of 3,000 at most: the lake stops below the third.
    TEST_CHECK(!lake.overflowing && !lake.playa && lake.cells == 2 && lake.level == 12.0f);
    TEST_CHECK(lake.inflow == 8000 && lake.evaporation == 8000);
    TEST_CHECK(lake.volume == (2.0 + 1.0) * t.grid.cellArea());
    TEST_CHECK(s.lakes.lakeOfCell[t.cell(3, 3)] == 0 && s.lakes.lakeOfCell[t.cell(4, 3)] == 0);
    TEST_CHECK(s.lakes.lakeOfCell[t.cell(5, 3)] == -1);
    TEST_CHECK(s.lakes.toOcean == 0 && s.lakes.runoff == evaporated(s.lakes));
}

void testFullLake() {
    const Terrain t = oneBasinIsland();
    const Solved s = solve(t, basinWater(t, 1000, 500));
    TEST_CHECK(s.lakes.lakes.size() == 1);
    const PlanetLake& lake = s.lakes.lakes[0];
    TEST_CHECK(lake.overflowing && lake.cells == 8 && lake.level == 40.0f);
    TEST_CHECK(lake.outflow == 4000 && lake.pass == t.cell(2, 3));
    TEST_CHECK(t.ocean[lake.partner] == terrain::planet::kOcean);
    TEST_CHECK(s.lakes.toOcean == 4000 && s.lakes.runoff == s.lakes.toOcean + evaporated(s.lakes));
}

void testPlaya() {
    const Terrain t = oneBasinIsland();
    const Solved s = solve(t, basinWater(t, 100, 3000));
    TEST_CHECK(s.lakes.lakes.size() == 1 && s.lakes.lakes[0].playa && s.lakes.lakes[0].cells == 0);
    TEST_CHECK(s.lakes.lakes[0].level == 10.0f && s.lakes.lakes[0].evaporation == 800);
}

void testFrozenBasin() {
    const Terrain t = oneBasinIsland();
    Table table = basinWater(t, 10, 0);
    for (std::size_t c = 0; c < t.grid.cells(); ++c) {
        table.frozen[c] = true;
    }
    const Solved s = solve(t, table);
    TEST_CHECK(s.lakes.lakes.size() == 1 && s.lakes.lakes[0].overflowing &&
               s.lakes.lakes[0].frozen);
    TEST_CHECK(s.lakes.toOcean == 80);
}

// Water on basin A only: A fills and spills into B (geolink), which is partial or full.
Table twoBasinWater(const Terrain& t, std::int64_t evaporationB) {
    Table table(t);
    for (const auto& [i, j] : {std::pair{3, 3}, {3, 4}, {4, 3}, {4, 4}, {5, 3}}) {
        table.runoff[t.cell(i, j)] = 1000;
        table.evaporation[t.cell(i, j)] = 500;
    }
    for (const auto& [i, j] : {std::pair{7, 3}, {7, 4}, {8, 3}, {8, 4}}) {
        table.evaporation[t.cell(i, j)] = evaporationB;
    }
    return table;
}

void testSpillIntoSibling() {
    const Terrain t = planet_test::twoBasinIsland();
    const Solved s = solve(t, twoBasinWater(t, 1000));
    TEST_CHECK(s.lakes.lakes.size() == 2);
    const PlanetLake& b = s.lakes.lakes[0]; // node 1
    const PlanetLake& a = s.lakes.lakes[1]; // node 2
    TEST_CHECK(a.node == 2 && a.overflowing && a.outflow == 2500 && a.partner == t.cell(7, 3));
    TEST_CHECK(b.node == 1 && !b.overflowing && b.inflow == 2500 && b.cells == 2 &&
               b.level == 20.0f);
    TEST_CHECK(s.lakes.state[3] == terrain::planet::DepressionState::Dry);
    TEST_CHECK(s.lakes.toOcean == 0 && s.lakes.runoff == evaporated(s.lakes));
}

void testBothFull() {
    const Terrain t = planet_test::twoBasinIsland();
    const Solved s = solve(t, twoBasinWater(t, 100));
    TEST_CHECK(s.lakes.lakes.size() == 1);
    const PlanetLake& merged = s.lakes.lakes[0];
    TEST_CHECK(merged.node == 3 && merged.overflowing && merged.cells == 12 &&
               merged.level == 40.0f);
    TEST_CHECK(merged.evaporation == 2900 && merged.outflow == 2100);
    TEST_CHECK(s.lakes.lakeOfCell[t.cell(7, 3)] == 0 && s.lakes.lakeOfCell[t.cell(6, 4)] == 0);
    TEST_CHECK(s.lakes.toOcean == 2100);
}

void testAcrossTrees() {
    // Along row 2: the sea, a 30 m sill, basin X (5, 20 m), a 40 m pass, basin R (10, 25 m);
    // walls of 100 m elsewhere. X reaches the sea first; R then spills over 40 m onto the slope
    // of X: attached to the ocean, its water still ends in X.
    Terrain t = planet_test::flatTerrain(12, 5, 100.0f);
    for (int j = 0; j < 5; ++j) {
        t.at(0, j) = -100.0f;
    }
    const float row[] = {30.0f, 5.0f, 20.0f, 40.0f, 10.0f, 25.0f};
    for (int i = 1; i <= 6; ++i) {
        t.at(i, 2) = row[i - 1];
    }
    t.markOcean();
    Table table(t);
    table.runoff[t.cell(5, 2)] = 3000;
    table.runoff[t.cell(6, 2)] = 3000;
    table.evaporation[t.cell(5, 2)] = 1000;
    table.evaporation[t.cell(6, 2)] = 1000;
    table.evaporation[t.cell(2, 2)] = 3000;
    table.evaporation[t.cell(3, 2)] = 3000;
    const Solved s = solve(t, table);
    TEST_CHECK(s.hierarchy.nodes[2].parent == terrain::planet::kOceanNode);
    TEST_CHECK(s.hierarchy.nodes[2].geolink == 1);
    TEST_CHECK(s.lakes.lakes.size() == 2);
    const PlanetLake& x = s.lakes.lakes[0];
    const PlanetLake& r = s.lakes.lakes[1];
    TEST_CHECK(r.overflowing && r.outflow == 4000 && r.partner == t.cell(3, 2));
    TEST_CHECK(!x.overflowing && x.inflow == 4000 && x.cells == 1 && x.level == 20.0f);
    TEST_CHECK(s.lakes.toOcean == 0 && s.lakes.runoff == evaporated(s.lakes));
}

void testConservation() {
    // A rough relief with its climate: every unit of runoff reaches the sea or evaporates.
    Terrain t = planet_test::noisyTerrain(96, 48, 9, -1500.0f, 2500.0f, 2);
    t.grid.cellSize = 50000.0;
    t.markOcean(1.0e10);
    const terrain::PlanetClimateParams params;
    const terrain::planet::PlanetClimate climate =
        terrain::planet::computeClimate(t.grid, t.height, t.ocean, params, 1);
    const std::vector<std::uint32_t> order = terrain::planet::sortCells(t.height, 1);
    const auto receiver = terrain::planet::computeReceivers(t.grid, t.height, t.land, 1);
    const auto hierarchy =
        terrain::planet::buildHierarchy(t.grid, t.height, t.land, order, receiver);
    const auto lakes = terrain::planet::solveLakes(
        t.grid, t.height, hierarchy, order,
        terrain::planet::climateWater(t.grid, t.height, t.ocean, climate, params));
    std::size_t terminal = 0;
    std::size_t overflowing = 0;
    for (const PlanetLake& lake : lakes.lakes) {
        (lake.overflowing ? overflowing : terminal) += 1;
    }
    TEST_CHECK(terminal > 0 && overflowing > 0);
    TEST_CHECK(lakes.runoff > 0 && lakes.runoff == lakes.toOcean + evaporated(lakes));
}

} // namespace

int main() {
    testTerminalLake();
    testFullLake();
    testPlaya();
    testFrozenBasin();
    testSpillIntoSibling();
    testBothFull();
    testAcrossTrees();
    testConservation();
    return test::result();
}

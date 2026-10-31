// Discharge of the planet level: the water of every cell routed to the sea, a lake or a closed
// basin, exactly conserved, and the drainage basins.
#include "terrain/generation/planet/planet_flow.hpp"
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
using terrain::planet::PlanetFlow;

struct Routed {
    terrain::planet::LandValues<std::uint8_t> receiver;
    LakeSolution lakes;
    PlanetFlow flow;
    std::vector<std::int32_t> basin;
};

Routed route(const Terrain& t, const terrain::planet::WaterOf& water) {
    Routed r;
    const std::vector<std::uint32_t> order = terrain::planet::sortCells(t.height, 1);
    r.receiver = terrain::planet::computeReceivers(t.grid, t.height, t.land, 1);
    const auto hierarchy =
        terrain::planet::buildHierarchy(t.grid, t.height, t.land, order, r.receiver);
    r.lakes = terrain::planet::solveLakes(t.grid, t.height, hierarchy, order, water);
    r.flow =
        terrain::planet::routeFlow(t.grid, t.height, t.land, r.receiver, order, r.lakes, water);
    const terrain::planet::LakeAt lakeAt = [&r](std::size_t c) { return r.lakes.lakeOfCell[c]; };
    r.basin = terrain::planet::computeBasins(t.grid, t.ocean, r.receiver.expand(), lakeAt,
                                             r.lakes.lakes);
    return r;
}

void testConservation() {
    Terrain t = planet_test::noisyTerrain(96, 48, 13, -1500.0f, 2500.0f, 2);
    t.grid.cellSize = 50000.0;
    t.markOcean(1.0e10);
    const terrain::PlanetClimateParams params;
    const auto climate = terrain::planet::computeClimate(t.grid, t.height, t.ocean, params, 1);
    const Routed r =
        route(t, terrain::planet::climateWater(t.grid, t.height, t.ocean, climate, params));
    TEST_CHECK(r.lakes.lakes.size() > 3);
    std::int64_t evaporation = 0;
    bool inflows = true;
    for (std::size_t l = 0; l < r.lakes.lakes.size(); ++l) {
        inflows = inflows && r.flow.lakeInflow[l] == r.lakes.lakes[l].inflow;
        evaporation += r.lakes.lakes[l].evaporation;
    }
    TEST_CHECK(inflows);
    TEST_CHECK(r.flow.toOcean == r.lakes.toOcean && r.lakes.runoff == r.flow.toOcean + evaporation);
    // The water only goes down, and every land cell ends at the sea, a lake or a closed basin.
    bool downhill = true;
    bool ends = true;
    for (std::size_t c = 0; c < t.grid.cells(); ++c) {
        ends =
            ends && (t.ocean[c] != terrain::planet::kLand ? r.basin[c] == terrain::planet::kNoBasin
                                                          : r.basin[c] >= 0);
        if (r.flow.discharge[c] > 0.0f && r.receiver[c] != terrain::kNoNeighbor) {
            const std::size_t n = t.grid.neighbor(c, r.receiver[c]);
            downhill = downhill && t.height[n] <= t.height[c] &&
                       (t.ocean[n] != terrain::planet::kLand || r.lakes.lakeOfCell[n] >= 0 ||
                        r.flow.discharge[n] >= r.flow.discharge[c]);
        }
    }
    TEST_CHECK(downhill && ends);
}

void testDesertCrossing() {
    // A valley along row 2 that rises 10 m per cell to the east, a wall at column 39 and the sea
    // at column 0. Only the mountains (columns 30 to 38) get water: it crosses the desert intact.
    Terrain t = planet_test::flatTerrain(40, 5, 0.0f);
    for (int j = 0; j < 5; ++j) {
        const int side = j < 2 ? 2 - j : j - 2;
        for (int i = 1; i < 39; ++i) {
            t.at(i, j) = 10.0f * static_cast<float>(i) + 1000.0f * static_cast<float>(side);
        }
        t.at(0, j) = -100.0f;
        t.at(39, j) = 5000.0f;
    }
    t.markOcean();
    std::vector<std::int64_t> runoff(t.grid.cells(), 0);
    for (int j = 0; j < 5; ++j) {
        for (int i = 30; i < 39; ++i) {
            runoff[t.cell(i, j)] = 1000;
        }
    }
    const Routed r = route(t, [&](std::size_t c) { return CellWater{runoff[c], 0, false}; });
    TEST_CHECK(r.lakes.lakes.empty());
    TEST_CHECK(r.flow.discharge[t.cell(1, 2)] ==
               static_cast<float>(terrain::planet::cubicMeters(45000)));
    TEST_CHECK(r.flow.discharge[t.cell(15, 2)] == r.flow.discharge[t.cell(1, 2)]);
    TEST_CHECK(r.flow.toOcean == 45000);
    // No spring in the desert: away from the valley, nothing flows.
    TEST_CHECK(r.flow.discharge[t.cell(10, 0)] == 0.0f && r.flow.discharge[t.cell(10, 4)] == 0.0f);
    TEST_CHECK(r.basin[t.cell(35, 0)] == static_cast<std::int32_t>(t.cell(1, 2)));
}

void testBasinsThroughLakes() {
    // Basin A spills into basin B, whose lake evaporates all of it: one basin, named after B.
    const Terrain t = planet_test::twoBasinIsland();
    std::vector<CellWater> table(t.grid.cells());
    for (const auto& [i, j] : {std::pair{3, 3}, {3, 4}, {4, 3}, {4, 4}, {5, 3}}) {
        table[t.cell(i, j)] = CellWater{1000, 500, false};
    }
    for (const auto& [i, j] : {std::pair{7, 3}, {7, 4}, {8, 3}, {8, 4}}) {
        table[t.cell(i, j)] = CellWater{0, 1000, false};
    }
    const Routed r = route(t, [&](std::size_t c) { return table[c]; });
    TEST_CHECK(r.lakes.lakes.size() == 2 && r.lakes.lakes[1].overflowing);
    const auto lakeB = static_cast<std::int32_t>(t.grid.cells()); // lake 0
    TEST_CHECK(r.basin[t.cell(3, 3)] == lakeB && r.basin[t.cell(8, 4)] == lakeB);
    TEST_CHECK(r.basin[t.cell(4, 3)] == lakeB && r.basin[t.cell(5, 3)] == lakeB);
    TEST_CHECK(r.flow.lakeInflow[0] == 2500 && r.flow.lakeInflow[1] == 5000);
    // The rim drains to the sea, each cell its own mouth.
    TEST_CHECK(r.basin[t.cell(9, 5)] == static_cast<std::int32_t>(t.cell(9, 5)));
    TEST_CHECK(r.basin[t.cell(0, 0)] == terrain::planet::kNoBasin);
}

} // namespace

int main() {
    testConservation();
    testDesertCrossing();
    testBasinsThroughLakes();
    return test::result();
}

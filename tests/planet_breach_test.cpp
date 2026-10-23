// The floors of the cells and the breaching of the shallow depressions of the planet level: which
// depressions are breached, how the way is carved, and the floors of the small world.
#include "terrain/generation/planet/planet_breach.hpp"

#include "planet_golden_support.hpp"
#include "planet_test_support.hpp"

#include "test_assert.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using planet_test::Terrain;
using terrain::PlanetBreach;
using terrain::PlanetBreachParams;
using terrain::planet::breachDepressions;

constexpr int kNx = 24;
constexpr int kNz = 8;

// A ramp rising 10 m per cell from the sea at column 0, a pit at (12, 4) depth meters below the
// lowest of its neighbors.
Terrain rampWithPit(float depth) {
    Terrain t = planet_test::flatTerrain(kNx, kNz, 0.0f);
    for (int j = 0; j < kNz; ++j) {
        for (int i = 0; i < kNx; ++i) {
            t.at(i, j) = i == 0 ? -100.0f : 10.0f * static_cast<float>(i);
        }
    }
    t.at(12, 4) = 110.0f - depth;
    t.markOcean(2.0e6);
    return t;
}

bool drains(const Terrain& t, std::size_t c) {
    for (int k = 0; k < 8; ++k) {
        if (terrain::planet::lowerCell(t.height, t.grid.neighbor(c, k), c)) {
            return true;
        }
    }
    return false;
}

void testShallowBreached() {
    Terrain t = rampWithPit(5.0f);
    const std::vector<float> before = t.height;
    const std::vector<std::uint8_t> mountain(t.grid.cells(), 0);
    const PlanetBreach b = breachDepressions(t.grid, t.height, t.ocean, mountain, {});
    TEST_CHECK(b.depressions == 1 && b.breached == 1 && b.keptArea == 0.0);
    TEST_CHECK(drains(t, t.cell(12, 4)));
    TEST_CHECK(b.cutCells >= 1 && b.deepestCut > 0.0f && b.deepestCut <= 5.01f);
    // Only lowered, never below the pit nor the sea level.
    for (std::size_t c = 0; c < t.grid.cells(); ++c) {
        TEST_CHECK(!(t.height[c] > before[c]));
        if (t.height[c] < before[c]) {
            TEST_CHECK(t.height[c] < before[t.cell(12, 4)] && !(t.height[c] < terrain::kSeaLevel));
        }
    }
}

void testDeepKept() {
    Terrain t = rampWithPit(100.0f);
    const std::vector<float> before = t.height;
    const std::vector<std::uint8_t> mountain(t.grid.cells(), 0);
    const PlanetBreach b = breachDepressions(t.grid, t.height, t.ocean, mountain, {});
    TEST_CHECK(b.depressions == 1 && b.breached == 0 && b.keptArea > 0.0);
    TEST_CHECK(t.height == before);
}

void testMountainsCutLess() {
    PlanetBreachParams p;
    p.depthLow = 60.0;
    p.depthHigh = 60.0;
    p.mountainDepth = 30.0;
    Terrain plain = rampWithPit(40.0f);
    Terrain range = plain;
    const std::vector<std::uint8_t> none(plain.grid.cells(), 0);
    std::vector<std::uint8_t> mountain = none;
    mountain[range.cell(12, 4)] = 1;
    TEST_CHECK(breachDepressions(plain.grid, plain.height, plain.ocean, none, p).breached == 1);
    TEST_CHECK(breachDepressions(range.grid, range.height, range.ocean, mountain, p).breached == 0);
}

void testBelowSeaKept() {
    Terrain t = rampWithPit(5.0f);
    // A shallow hollow below the sea level, too small for the ocean: the sea would flow in.
    t.at(5, 2) = -5.0f;
    t.markOcean(2.0e6);
    TEST_CHECK(t.ocean[t.cell(5, 2)] == terrain::planet::kLand);
    const std::vector<std::uint8_t> mountain(t.grid.cells(), 0);
    const PlanetBreach b = breachDepressions(t.grid, t.height, t.ocean, mountain, {});
    TEST_CHECK(b.depressions == 2 && b.breached == 1);
    TEST_CHECK(t.at(5, 2) == -5.0f && !drains(t, t.cell(5, 2)));
}

// Breached without a limit, a noisy land drains entirely to the sea: a second pass finds no
// depression left.
void testEverythingDrains() {
    Terrain t = planet_test::noisyTerrain(40, 20, 7, 10.0f, 600.0f, 1);
    for (int j = 0; j < 20; ++j) {
        t.at(0, j) = -100.0f;
    }
    t.markOcean(2.0e6);
    PlanetBreachParams p;
    p.depthLow = 1.0e9;
    p.depthHigh = 1.0e9;
    const std::vector<std::uint8_t> mountain(t.grid.cells(), 0);
    const PlanetBreach first = breachDepressions(t.grid, t.height, t.ocean, mountain, p);
    TEST_CHECK(first.depressions > 0 && first.breached == first.depressions);
    const PlanetBreach second = breachDepressions(t.grid, t.height, t.ocean, mountain, p);
    TEST_CHECK(second.depressions == 0);
    std::printf("noisy land: %zu depressions breached, %zu cells cut, deepest %.1f m\n",
                first.depressions, first.cutCells, static_cast<double>(first.deepestCut));
}

// The floors of the small world: the lowest of the 3 x 3 points of the same water as the center,
// never above the center, a land point for a land cell.
void testFloors() {
    const terrain::Heightfield field(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                                     golden::kSmallPlanet);
    const terrain::PlanetGrid grid = terrain::makePlanetGrid(field.world(), 65536.0);
    const terrain::planet::CellRelief cells = terrain::planet::sampleCells(field, grid, 1, 2, 3);
    std::size_t floors = 0;
    bool below = true;
    bool same = true;
    for (std::size_t c = 0; c < grid.cells(); ++c) {
        const terrain::HeightSample center =
            field.sampleAt(grid.centerX(grid.column(c)), grid.centerZ(grid.row(c)));
        below = below && !(cells.height[c] > center.height);
        if (cells.floor[c] == terrain::planet::kCenterFloor) {
            continue;
        }
        ++floors;
        const auto p = terrain::planet::floorPoint(grid, c, cells.floor[c], 3);
        const terrain::HeightSample s = field.sampleAt(p[0], p[1]);
        same = same && s.height == cells.height[c] &&
               (s.water == terrain::WaterKind::Ocean) == (cells.sea[c] != 0) &&
               (s.mountainMask > 0.0f) == (cells.mountain[c] != 0);
    }
    TEST_CHECK(floors > grid.cells() / 4 && below && same);
}

} // namespace

int main() {
    testShallowBreached();
    testDeepKept();
    testMountainsCutLess();
    testBelowSeaKept();
    testEverythingDrains();
    testFloors();
    return test::result();
}

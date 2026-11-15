// The depressions below the sea level of the planet level sealed by their way out (agent 52,
// PlanetBreachParams::sealByOutlet): on a ramp from the sea, a trench below the sea level (it
// spills to the sea) and, upslope, a pit below the sea level whose way out crosses the trench.
// - off: both sealed, nothing cut (the level of before; the trench, level, is one depression
//   per outlet);
// - on: the pit is breached toward the trench, its cut below the sea level stopping there; the
//   trench stays sealed (its way out reaches the sea), the land between it and the sea untouched;
// - on, a hollow below the sea level next to the sea stays sealed.
#include "terrain/generation/planet/planet_breach.hpp"

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
constexpr int kTrench = 6;
constexpr int kPit = 12;

// The sea on columns 0 and 1 (16 km^2: the ocean), a ramp rising 10 m per column, a trench at
// -100 m on column kTrench (8 km^2: land) and a pit at -30 m at (kPit, 4).
Terrain trenchAndPit() {
    Terrain t = planet_test::flatTerrain(kNx, kNz, 0.0f);
    for (int j = 0; j < kNz; ++j) {
        for (int i = 0; i < kNx; ++i) {
            t.at(i, j) = i < 2 ? -100.0f : i == kTrench ? -100.0f : 10.0f * static_cast<float>(i);
        }
    }
    t.at(kPit, 4) = -30.0f;
    t.markOcean(1.0e7);
    return t;
}

PlanetBreachParams deep(bool sealByOutlet) {
    PlanetBreachParams p;
    p.depthLow = 500.0;
    p.depthHigh = 500.0;
    p.sealByOutlet = sealByOutlet;
    return p;
}

bool drains(const Terrain& t, std::size_t c) {
    for (int k = 0; k < 8; ++k) {
        if (terrain::planet::lowerCell(t.height, t.grid.neighbor(c, k), c)) {
            return true;
        }
    }
    return false;
}

void testOff() {
    Terrain t = trenchAndPit();
    TEST_CHECK(t.ocean[t.cell(0, 0)] != terrain::planet::kLand);
    TEST_CHECK(t.ocean[t.cell(kTrench, 0)] == terrain::planet::kLand);
    const std::vector<float> before = t.height;
    const std::vector<std::uint8_t> mountain(t.grid.cells(), 0);
    const PlanetBreach b = breachDepressions(t.grid, t.height, t.ocean, mountain, deep(false));
    std::printf("off: %zu depressions, %zu breached\n", b.depressions, b.breached);
    TEST_CHECK(b.depressions >= 2 && b.breached == 0);
    TEST_CHECK(t.height == before);
}

void testOn() {
    Terrain t = trenchAndPit();
    const std::vector<float> before = t.height;
    const std::vector<std::uint8_t> mountain(t.grid.cells(), 0);
    const PlanetBreach b = breachDepressions(t.grid, t.height, t.ocean, mountain, deep(true));
    std::printf("on: %zu depressions, %zu breached, %zu cells cut, deepest %.1f m\n",
                b.depressions, b.breached, b.cutCells, static_cast<double>(b.deepestCut));
    TEST_CHECK(b.depressions >= 2 && b.breached == 1);
    TEST_CHECK(drains(t, t.cell(kPit, 4)));
    std::size_t below = 0;
    for (int j = 0; j < kNz; ++j) {
        for (int i = 0; i < kNx; ++i) {
            const float h = t.at(i, j);
            const float was = before[t.cell(i, j)];
            TEST_CHECK(!(h > was));
            // Nothing cut between the trench and the sea, nor in the trench.
            TEST_CHECK(i > kTrench || h == was);
            if (h < was) {
                TEST_CHECK(h < -30.0f && h > -100.0f);
                ++below;
            }
        }
    }
    TEST_CHECK(below >= kPit - kTrench - 1);
}

void testNextToTheSea() {
    Terrain t = planet_test::flatTerrain(kNx, kNz, 0.0f);
    for (int j = 0; j < kNz; ++j) {
        for (int i = 0; i < kNx; ++i) {
            t.at(i, j) = i == 0 ? -100.0f : 10.0f * static_cast<float>(i);
        }
    }
    t.at(5, 2) = -5.0f;
    t.markOcean(2.0e6);
    const std::vector<float> before = t.height;
    const std::vector<std::uint8_t> mountain(t.grid.cells(), 0);
    const PlanetBreach b = breachDepressions(t.grid, t.height, t.ocean, mountain, deep(true));
    TEST_CHECK(b.depressions == 1 && b.breached == 0);
    TEST_CHECK(t.height == before && !drains(t, t.cell(5, 2)));
}

} // namespace

int main() {
    testOff();
    testOn();
    testNextToTheSea();
    return test::result();
}

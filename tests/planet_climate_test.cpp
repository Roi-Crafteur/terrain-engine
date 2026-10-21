// Continental climate of the planet level: prevailing winds by latitude band, moisture carried
// from the oceans, rain shadows behind the ranges, deserts, evapotranspiration and ice.
#include "terrain/generation/climate.hpp"
#include "terrain/generation/planet/planet_climate.hpp"
#include "terrain/generation/planet/planet_relief.hpp"
#include "terrain/generation/planet/planet_wind.hpp"

#include "test_assert.hpp"

#include <bit>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using terrain::PlanetClimateParams;
using terrain::PlanetGrid;
using terrain::planet::PlanetClimate;

// 9,600 x 3,600 km in cells of 100 km: rows of 5 degrees of latitude. Row 9 is at 42.5 degrees
// north (westerlies), row 15 at 12.5 degrees north (trades), row 17 at 2.5 degrees north.
const PlanetGrid kGrid{96, 36, 100000.0};
constexpr int kWesterlyRow = 9;
constexpr int kTradeRow = 15;

struct World {
    std::vector<float> height;
    std::vector<std::uint8_t> ocean;
};

// Ocean 1,000 m deep, with a flat continent of 200 m from column 30 to 69 and, if ridge, a
// north-south range of 3,000 m from column 48 to 51 across it.
World makeWorld(const PlanetGrid& grid, bool continent, bool ridge) {
    World world;
    world.height.assign(grid.cells(), -1000.0f);
    const int scale = grid.nx / kGrid.nx;
    for (int j = 0; j < grid.nz; ++j) {
        for (int i = 0; i < grid.nx; ++i) {
            const int column = i / scale;
            if (continent && column >= 30 && column < 70) {
                const bool range = ridge && column >= 48 && column < 52;
                world.height[grid.index(i, j)] = range ? 3000.0f : 200.0f;
            }
        }
    }
    world.ocean = terrain::planet::markOcean(grid, world.height, 1.0e10);
    return world;
}

PlanetClimate climateOf(const PlanetGrid& grid, const World& world, int threads = 1) {
    return terrain::planet::computeClimate(grid, world.height, world.ocean, PlanetClimateParams{},
                                           threads);
}

float rain(const PlanetClimate& climate, int i, int j) {
    return climate.precipitation[kGrid.index(i, j)];
}

double rowMean(const PlanetClimate& climate, int j) {
    double sum = 0.0;
    for (int i = 0; i < kGrid.nx; ++i) {
        sum += rain(climate, i, j);
    }
    return sum / kGrid.nx;
}

void testWinds() {
    const PlanetClimateParams params;
    const auto wind = [&](int j) { return terrain::planet::rowWind(kGrid, j, params); };
    TEST_CHECK(std::abs(terrain::planet::rowLatitude(kGrid, kTradeRow) - 12.5) < 1e-12);
    TEST_CHECK(wind(kTradeRow).x < 0.0 && wind(kTradeRow).z > 0.0);       // to the SW
    TEST_CHECK(wind(kWesterlyRow).x > 0.0 && wind(kWesterlyRow).z < 0.0); // to the NE
    TEST_CHECK(wind(2).x < 0.0 && wind(2).z > 0.0);                       // polar, to the SW
    // The southern hemisphere mirrors the northern one.
    for (int j = 0; j < kGrid.nz / 2; ++j) {
        const int mirror = kGrid.nz - 1 - j;
        TEST_CHECK(wind(j).x == wind(mirror).x && wind(j).z == -wind(mirror).z);
        TEST_CHECK(std::abs(wind(j).x * wind(j).x + wind(j).z * wind(j).z - 1.0) < 1e-12);
    }
}

void testMoistureRows() {
    const terrain::planet::MoistureRows rows =
        terrain::planet::moistureRows(kGrid, PlanetClimateParams{});
    // Divergence at 30 degrees (rows 11 and 12) and at the poles (rows 0 and 35): neither row
    // feeds the other. Convergence at the equator (rows 17 and 18) and at 60 degrees.
    TEST_CHECK(rows.moistureFrom[11] != 12 && rows.moistureFrom[12] != 11);
    TEST_CHECK(rows.moistureFrom[0] != 35 && rows.moistureFrom[35] != 0);
    TEST_CHECK(rows.moistureFrom[17] == 16 && rows.moistureFrom[18] == 19);
    TEST_CHECK(rows.moistureFrom[5] == 4 && rows.moistureFrom[6] == 7);
    // Every row is in exactly one chain, after the row it takes its moisture from.
    std::vector<int> seen(36, 0);
    for (const std::vector<int>& chain : rows.chains) {
        TEST_CHECK(!chain.empty() && rows.moistureFrom[static_cast<std::size_t>(chain[0])] == -1);
        for (std::size_t k = 0; k < chain.size(); ++k) {
            ++seen[static_cast<std::size_t>(chain[k])];
            if (k > 0) {
                TEST_CHECK(rows.moistureFrom[static_cast<std::size_t>(chain[k])] == chain[k - 1]);
            }
        }
    }
    for (int count : seen) {
        TEST_CHECK(count == 1);
    }
    TEST_CHECK(rows.chains.size() == 6);
}

void testAquaplanet() {
    const PlanetClimate climate = climateOf(kGrid, makeWorld(kGrid, false, false));
    const double equator = rowMean(climate, 17);
    const double subtropics = rowMean(climate, 12);  // 27.5 degrees
    const double midLatitudes = rowMean(climate, 7); // 52.5 degrees
    const double pole = rowMean(climate, 0);
    std::printf("aquaplanet: equator %.0f, 27.5 deg %.0f, 52.5 deg %.0f, pole %.0f mm/yr\n",
                equator, subtropics, midLatitudes, pole);
    TEST_CHECK(equator > 2.0 * subtropics);
    TEST_CHECK(midLatitudes > 1.2 * subtropics);
    TEST_CHECK(pole < midLatitudes);
    for (int j = 0; j < kGrid.nz / 2; ++j) {
        const double a = rowMean(climate, j);
        const double b = rowMean(climate, kGrid.nz - 1 - j);
        TEST_CHECK(std::abs(a - b) <= 1e-3 * (a + b));
    }
}

void testRainShadow() {
    const PlanetClimate ridge = climateOf(kGrid, makeWorld(kGrid, true, true));
    const PlanetClimate flat = climateOf(kGrid, makeWorld(kGrid, true, false));
    std::printf("westerlies: windward %.0f, lee %.0f (flat %.0f); trades: windward %.0f, "
                "lee %.0f (flat %.0f) mm/yr\n",
                rain(ridge, 44, kWesterlyRow), rain(ridge, 57, kWesterlyRow),
                rain(flat, 57, kWesterlyRow), rain(ridge, 55, kTradeRow),
                rain(ridge, 42, kTradeRow), rain(flat, 42, kTradeRow));
    // The westerlies blow to the east: dry behind the range, east of it.
    TEST_CHECK(rain(ridge, 44, kWesterlyRow) > 2.0f * rain(ridge, 57, kWesterlyRow));
    TEST_CHECK(rain(ridge, 57, kWesterlyRow) < 0.7f * rain(flat, 57, kWesterlyRow));
    // The trades blow to the west: the shadow is on the west side.
    TEST_CHECK(rain(ridge, 55, kTradeRow) > 2.0f * rain(ridge, 42, kTradeRow));
    TEST_CHECK(rain(ridge, 42, kTradeRow) < 0.7f * rain(flat, 42, kTradeRow));
    // The range itself is wetter than the plain in front of it.
    TEST_CHECK(rain(ridge, 49, kWesterlyRow) > rain(flat, 49, kWesterlyRow));
}

void testCoasts() {
    const PlanetClimate climate = climateOf(kGrid, makeWorld(kGrid, true, false));
    std::printf("coasts: trades east %.0f west %.0f; westerlies west %.0f east %.0f mm/yr\n",
                rain(climate, 68, kTradeRow), rain(climate, 31, kTradeRow),
                rain(climate, 31, kWesterlyRow), rain(climate, 68, kWesterlyRow));
    // Trades: the east coast is on the wind, the west coast behind the whole continent.
    TEST_CHECK(rain(climate, 68, kTradeRow) > 1.5f * rain(climate, 31, kTradeRow));
    // Westerlies: the other way around (the evapotranspiration recycled over land keeps the
    // east coast humid too).
    TEST_CHECK(rain(climate, 31, kWesterlyRow) > 1.25f * rain(climate, 68, kWesterlyRow));
}

void testValuesAndThreads() {
    const World world = makeWorld(kGrid, true, true);
    const PlanetClimate one = climateOf(kGrid, world, 1);
    const PlanetClimate three = climateOf(kGrid, world, 3);
    bool same = one.rowTemperature == three.rowTemperature;
    bool normal = true;
    for (std::size_t c = 0; c < kGrid.cells(); ++c) {
        const float p = one.precipitation[c];
        same = same && std::bit_cast<std::uint32_t>(p) ==
                           std::bit_cast<std::uint32_t>(three.precipitation[c]);
        normal = normal && std::isfinite(p) && (p == 0.0f || p >= FLT_MIN);
    }
    TEST_CHECK(same);
    TEST_CHECK(normal);
}

void testTemperatureAndIce() {
    const PlanetClimateParams params;
    for (const terrain::WorldSize& world :
         {terrain::WorldSize{}, terrain::WorldSize{32.0 * 65536.0, 16.0 * 65536.0}}) {
        const terrain::ClimateField field(1, terrain::ClimateParams{}, world);
        for (const double z : {-0.5 * world.z, -0.3 * world.z, -12345.0, 0.0, 0.25 * world.z}) {
            TEST_CHECK(terrain::planet::latitudeTemperature(z, world.z, params) ==
                       field.latitudeTemperatureAt(z));
        }
    }
    const PlanetClimate climate = climateOf(kGrid, makeWorld(kGrid, true, false));
    TEST_CHECK(climate.rowTemperature[0] < params.iceTemperature);
    TEST_CHECK(climate.rowTemperature[17] > 25.0f);
    const terrain::planet::CellWater polar = terrain::planet::cellWater(
        kGrid, climate.rowTemperature[0], 200.0f, 300.0f, terrain::planet::kLand, params);
    TEST_CHECK(polar.frozen && polar.lakeEvaporation == 0);
    TEST_CHECK(polar.runoff == terrain::planet::waterUnits(300.0, kGrid.cellArea()));
    const terrain::planet::CellWater sea = terrain::planet::cellWater(
        kGrid, climate.rowTemperature[17], -100.0f, 3000.0f, terrain::planet::kOcean, params);
    TEST_CHECK(sea.runoff == 0 && sea.lakeEvaporation == 0 && !sea.frozen);
    // Altitude cools: 6.5 degrees per km.
    TEST_CHECK(terrain::planet::cellTemperature(20.0f, 2000.0f, params) == 20.0f - 13.0f);
    TEST_CHECK(terrain::planet::cellTemperature(20.0f, -500.0f, params) == 20.0f);
}

void testRunoff() {
    const PlanetClimateParams params;
    // A desert gives no runoff at all, so no river springs there.
    TEST_CHECK(terrain::planet::landRunoff(100.0, 1800.0, params) == 0.0);
    const double wet = terrain::planet::landRunoff(2000.0, 1000.0, params);
    TEST_CHECK(wet > 1000.0 && wet < 2000.0);
    TEST_CHECK(terrain::planet::landRunoff(500.0, 0.0, params) == 500.0);
    TEST_CHECK(terrain::planet::lakeEvaporation(2000.0, 1000.0, wet) >= 0.0);
    TEST_CHECK(terrain::planet::lakeEvaporation(100.0, 1800.0, 0.0) == 1700.0);
    TEST_CHECK(terrain::planet::potentialEvapotranspiration(-5.0, params) == 0.0);
    // 1,000 mm/yr over 1 km^2 is 0.031688 m^3/s.
    TEST_CHECK(terrain::planet::waterUnits(1000.0, 1.0e6) == 31688);
    TEST_CHECK(terrain::planet::cubicMeters(31688) == 0.031688);
}

void testResolution() {
    // The rates are lengths in meters: halving the cells changes the climate little.
    const PlanetGrid fine{192, 72, 50000.0};
    const PlanetClimate coarse = climateOf(kGrid, makeWorld(kGrid, true, true));
    const PlanetClimate detailed = climateOf(fine, makeWorld(fine, true, true), 2);
    double a = 0.0;
    double b = 0.0;
    for (int j = 0; j < kGrid.nz; ++j) {
        for (int i = 30; i < 70; ++i) {
            a += rain(coarse, i, j);
            for (int q = 0; q < 4; ++q) {
                b += 0.25 * detailed.precipitation[fine.index(2 * i + q % 2, 2 * j + q / 2)];
            }
        }
    }
    std::printf("land mean: %.0f mm/yr at 100 km, %.0f at 50 km\n", a / 1440.0, b / 1440.0);
    TEST_CHECK(std::abs(a - b) < 0.2 * a);
}

} // namespace

int main() {
    testWinds();
    testMoistureRows();
    testAquaplanet();
    testRainShadow();
    testCoasts();
    testValuesAndThreads();
    testTemperatureAndIce();
    testRunoff();
    testResolution();
    return test::result();
}

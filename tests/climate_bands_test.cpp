// The bands of the winds of the planet level joined smoothly (planet_bands.hpp, task 11C step 1):
// no straight line of the rain where the bands meet, the great zones of the climate kept, the
// lines waving with the longitude (flag smooth_bands); without it the climate of main, bit for bit.
#include "terrain/generation/planet/planet_bands.hpp"
#include "terrain/generation/planet/planet_climate.hpp"
#include "terrain/generation/planet/planet_relief.hpp"

#include "planet_golden_support.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using terrain::PlanetClimateParams;
using terrain::PlanetGrid;
using terrain::planet::PlanetClimate;
using terrain::planet::WindBands;

// 60,000 x 36,000 km in cells of 100 km: rows of half a degree.
const PlanetGrid kGrid{600, 360, 100000.0};
constexpr std::uint64_t kSeed = 20261019;

// The bands as main marches them (no flag) and joined (flag smooth_bands).
PlanetClimateParams atOnce() {
    return PlanetClimateParams{};
}

PlanetClimateParams joined() {
    PlanetClimateParams params;
    params.smoothBands = true;
    return params;
}

void testWeights() {
    const PlanetClimateParams params = joined();
    const WindBands bands(kGrid, params, kSeed);
    const double reach = params.bandJoin + params.bandShift;
    bool sums = true;
    bool pairs = true;
    bool alone = true;
    for (int j = 0; j < kGrid.nz; ++j) {
        double nearest = 90.0;
        for (int k = 0; k < terrain::planet::kBandLines; ++k) {
            nearest = std::min(nearest, std::abs(bands.latitude(j) - WindBands::nominal(k)));
        }
        for (int i = 0; i < kGrid.nx; ++i) {
            double sum = 0.0;
            int weighing = 0;
            for (int b = 0; b < terrain::planet::kWindBands; ++b) {
                const float w = bands.weight(b, i, j);
                sum += w;
                weighing += w > 0.0f ? 1 : 0;
            }
            sums = sums && std::abs(sum - 1.0) < 1e-6;
            pairs = pairs && weighing >= 1 && weighing <= bands.bandsOf(j) && weighing <= 2;
            alone = alone && (nearest <= reach || weighing == 1);
        }
    }
    TEST_CHECK(sums);
    TEST_CHECK(pairs);
    TEST_CHECK(alone);
    // Each chain: contiguous rows in the order of its wind, every row it weighs on.
    for (int b = 0; b < terrain::planet::kWindBands; ++b) {
        const std::vector<int> chain = bands.chain(b);
        TEST_CHECK(!chain.empty());
        const int step = bands.wind(b).z > 0.0 ? 1 : -1;
        for (std::size_t n = 1; n < chain.size(); ++n) {
            TEST_CHECK(chain[n] == chain[n - 1] + step);
        }
        for (int j = 0; j < kGrid.nz; ++j) {
            const bool in = std::find(chain.begin(), chain.end(), j) != chain.end();
            bool weighs = false;
            for (int i = 0; i < kGrid.nx; ++i) {
                weighs = weighs || bands.weight(b, i, j) > 0.0f;
            }
            TEST_CHECK(in == weighs);
        }
    }
}

void testLines() {
    const PlanetClimateParams params = joined();
    const WindBands bands(kGrid, params, kSeed);
    const WindBands other(kGrid, params, kSeed + 1);
    double differs = 0.0;
    for (int k = 0; k < terrain::planet::kBandLines; ++k) {
        double low = 90.0;
        double high = -90.0;
        double steepest = 0.0;
        for (int i = 0; i < kGrid.nx; ++i) {
            const double at = bands.line(k, i);
            low = std::min(low, at);
            high = std::max(high, at);
            const double next = bands.line(k, (i + 1) % kGrid.nx); // across the seam too
            steepest = std::max(steepest, std::abs(next - at));
            differs = std::max(differs, std::abs(at - other.line(k, i)));
        }
        std::printf("line %d: %.2f to %.2f degrees, at most %.3f degree from a column to the next\n",
                    k, low, high, steepest);
        TEST_CHECK(high - low > 1.0); // never a straight line
        TEST_CHECK(low >= WindBands::nominal(k) - params.bandShift);
        TEST_CHECK(high <= WindBands::nominal(k) + params.bandShift);
        TEST_CHECK(steepest < 0.6); // a slow wave: under 0.6 degree of latitude per 100 km
    }
    TEST_CHECK(differs > 0.5); // the seed draws them
}

// Without the flag, the march by bands is the march of main: the small world of the goldens gives
// the golden relief, climate, hydrology and level of main, bit for bit.
void testOffIsMain() {
    const terrain::Heightfield field(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                                     golden::kSmallPlanet);
    const terrain::PlanetBuild build =
        terrain::planet::buildPlanet(field, golden::smallPlanetParams(1, 2));
    const std::vector<golden::Part> parts = golden::planetParts(
        {"relief", "climate", "hydrology", "level"}, build,
        {0x3F4264CF06FB5493ULL, 0xAB9EB405E174EAA4ULL, 0x27B5C16AD4EC6D79ULL,
         0xCDBFA06D845540C8ULL});
    for (const golden::Part& part : parts) {
        std::printf("no flag: %s %016llx\n", part.name,
                    static_cast<unsigned long long>(part.actual));
        TEST_CHECK(part.actual == part.expected);
    }
}

// An ocean 1,000 m deep with two flat continents of 200 m from 70 degrees south to 70 north.
struct World {
    std::vector<float> height;
    std::vector<std::uint8_t> ocean;
};
World makeWorld() {
    World world;
    world.height.assign(kGrid.cells(), -1000.0f);
    for (int j = 20; j < kGrid.nz - 20; ++j) {
        for (int i = 0; i < kGrid.nx; ++i) {
            if ((i >= 100 && i < 260) || (i >= 380 && i < 450)) {
                world.height[kGrid.index(i, j)] = 200.0f;
            }
        }
    }
    world.ocean = terrain::planet::markOcean(kGrid, world.height, 1.0e10);
    return world;
}

// How much the mean of log(rain) jumps from row j to row j + 1, beyond the change of its
// neighbors (a straight line of the rain across every column).
std::vector<double> rowJumps(const PlanetClimate& climate) {
    std::vector<double> step(static_cast<std::size_t>(kGrid.nz - 1), 0.0);
    for (int j = 0; j + 1 < kGrid.nz; ++j) {
        double sum = 0.0;
        for (int i = 0; i < kGrid.nx; ++i) {
            sum += std::log(climate.precipitation[kGrid.index(i, j + 1)] + 1.0) -
                   std::log(climate.precipitation[kGrid.index(i, j)] + 1.0);
        }
        step[static_cast<std::size_t>(j)] = sum / kGrid.nx;
    }
    std::vector<double> jump(step.size(), 0.0);
    for (std::size_t j = 1; j + 1 < step.size(); ++j) {
        jump[j] = std::abs(step[j] - 0.5 * (step[j - 1] + step[j + 1]));
    }
    return jump;
}

double zoneMean(const PlanetClimate& climate, const WindBands& bands, double from, double to) {
    double sum = 0.0;
    int count = 0;
    for (int j = 0; j < kGrid.nz; ++j) {
        const double at = std::abs(bands.latitude(j));
        if (at < from || at > to) {
            continue;
        }
        for (int i = 0; i < kGrid.nx; ++i) {
            sum += climate.precipitation[kGrid.index(i, j)];
            ++count;
        }
    }
    return sum / count;
}

void testNoStraightLine() {
    const World world = makeWorld();
    const PlanetClimate before =
        terrain::planet::computeClimate(kGrid, world.height, world.ocean, atOnce(), 4, kSeed);
    const PlanetClimate after =
        terrain::planet::computeClimate(kGrid, world.height, world.ocean, joined(), 4, kSeed);
    const WindBands bands(kGrid, joined(), kSeed);
    const std::vector<double> was = rowJumps(before);
    const std::vector<double> now = rowJumps(after);
    // The jumps at the lines (the rows within a degree of a nominal line), against the others.
    double atLinesBefore = 0.0;
    double atLinesNow = 0.0;
    std::vector<double> elsewhere;
    for (std::size_t j = 2; j + 2 < now.size(); ++j) {
        const double at = 0.5 * (bands.latitude(static_cast<int>(j)) +
                                 bands.latitude(static_cast<int>(j) + 1));
        bool line = false;
        for (int k = 0; k < terrain::planet::kBandLines; ++k) {
            line = line || std::abs(at - WindBands::nominal(k)) < 1.0;
        }
        if (line) {
            atLinesBefore = std::max(atLinesBefore, was[j]);
            atLinesNow = std::max(atLinesNow, now[j]);
        } else if (std::abs(at) < 85.0) {
            elsewhere.push_back(now[j]);
        }
    }
    std::sort(elsewhere.begin(), elsewhere.end());
    const double ordinary = elsewhere[elsewhere.size() * 95 / 100];
    std::printf("jumps of the rain at the lines: %.4f before, %.4f now; elsewhere %.4f (95th)\n",
                atLinesBefore, atLinesNow, ordinary);
    TEST_CHECK(atLinesBefore > 10.0 * ordinary); // the lines of before
    TEST_CHECK(atLinesNow <= 2.0 * ordinary);    // gone
    // The great zones of the climate stay: the wet tropics, the dry subtropics, the wet
    // westerlies, the dry poles.
    const double zones[4][2] = {{0.0, 10.0}, {18.0, 24.0}, {38.0, 52.0}, {70.0, 85.0}};
    for (const auto& zone : zones) {
        const double a = zoneMean(before, bands, zone[0], zone[1]);
        const double b = zoneMean(after, bands, zone[0], zone[1]);
        std::printf("zone %.0f-%.0f degrees: %.0f mm/yr before, %.0f now\n", zone[0], zone[1], a,
                    b);
        TEST_CHECK(std::abs(b - a) <= 0.1 * a);
    }
}

void testThreads() {
    const World world = makeWorld();
    const PlanetClimateParams params = joined();
    const PlanetClimate one =
        terrain::planet::computeClimate(kGrid, world.height, world.ocean, params, 1, kSeed);
    const PlanetClimate four =
        terrain::planet::computeClimate(kGrid, world.height, world.ocean, params, 4, kSeed);
    bool same = one.rowTemperature == four.rowTemperature;
    for (std::size_t c = 0; c < kGrid.cells(); ++c) {
        same = same && std::bit_cast<std::uint32_t>(one.precipitation[c]) ==
                           std::bit_cast<std::uint32_t>(four.precipitation[c]);
    }
    TEST_CHECK(same);
}

} // namespace

int main() {
    testWeights();
    testLines();
    testOffIsMain();
    testNoStraightLine();
    testThreads();
    return test::result();
}

// The figures of the floors of the seas (agent 51, io/sea_stats.hpp) on the small world: shares
// of the hypsometry that add up, depths within the bounds of the relief, profiles and windows
// measured, and the same figures whatever the number of threads.

#include "terrain/generation/heightfield.hpp"
#include "terrain/io/sea_stats.hpp"

#include "planet_golden_support.hpp"
#include "test_assert.hpp"

#include <cmath>
#include <cstdio>

namespace {

using terrain::SeaStats;

terrain::SeaStatsParams smallParams(int threads) {
    terrain::SeaStatsParams p;
    p.cellSize = 8'192.0;
    p.transects = 64;
    p.transectLength = 400'000.0;
    p.minLength = 150'000.0;
    p.openDistance = 150'000.0;
    p.hillWindows = 8;
    p.threads = threads;
    return p;
}

bool sameSpread(const terrain::SeaSpread& a, const terrain::SeaSpread& b) {
    return a.count == b.count && a.p10 == b.p10 && a.median == b.median && a.p90 == b.p90;
}

bool same(const SeaStats& a, const SeaStats& b) {
    bool equal = a.cells == b.cells && a.seaCells == b.seaCells && a.bands == b.bands &&
                 a.meanDepth == b.meanDepth && a.medianDepth == b.medianDepth &&
                 a.deepest == b.deepest && a.openCells == b.openCells &&
                 a.openMedianDepth == b.openMedianDepth && a.hillRms == b.hillRms &&
                 a.transects.size() == b.transects.size();
    equal = equal && sameSpread(a.breakDepth, b.breakDepth) &&
            sameSpread(a.slopeDegrees, b.slopeDegrees) &&
            sameSpread(a.riseDegrees, b.riseDegrees) && sameSpread(a.trenchDepth, b.trenchDepth);
    return equal;
}

void testSmallWorld() {
    const terrain::Heightfield field(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                                     golden::kSmallPlanet);
    const SeaStats one = terrain::measureSeas(field, smallParams(1));
    const SeaStats four = terrain::measureSeas(field, smallParams(4));
    std::printf("small world: %zu sea cells of %zu, mean depth %.0f m, deepest %.0f m, %zu "
                "profiles, shelf break %.0f m at %.0f km, slope %.2f deg, rise %.2f deg, hills "
                "rms %.1f m (%zu windows)\n",
                one.seaCells, one.cells, one.meanDepth, one.deepest, one.transects.size(),
                one.breakDepth.median, one.breakDistance.median * 1.0e-3, one.slopeDegrees.median,
                one.riseDegrees.median, one.hillRms, one.hillWindows);
    TEST_CHECK(one.cells == 256u * 128u);
    TEST_CHECK(one.seaCells > one.cells / 4 && one.seaCells < one.cells);
    double sum = 0.0;
    for (double share : one.bands) {
        TEST_CHECK(share >= 0.0 && share <= 1.0);
        sum += share;
    }
    TEST_CHECK(std::abs(sum - 1.0) < 1e-9);
    TEST_CHECK(one.meanDepth > 0.0 && one.meanDepth < 8'000.0);
    TEST_CHECK(one.medianDepth > 0.0 && one.deepest >= one.medianDepth);
    TEST_CHECK(one.deepest < 12'000.0);
    TEST_CHECK(!one.transects.empty() && one.breakDepth.count > 0);
    TEST_CHECK(one.hillWindows > 0 && one.hillRms >= 0.0);
    TEST_CHECK(same(one, four));
}

void testSpread() {
    const terrain::SeaSpread none = terrain::spreadOf({});
    TEST_CHECK(none.count == 0);
    const terrain::SeaSpread s =
        terrain::spreadOf({5.0, 1.0, 4.0, 2.0, 3.0, 6.0, 7.0, 8.0, 9.0, 10.0});
    TEST_CHECK(s.count == 10 && s.p10 == 2.0 && s.median == 6.0 && s.p90 == 10.0);
}

} // namespace

int main() {
    testSpread();
    testSmallWorld();
    return test::result();
}

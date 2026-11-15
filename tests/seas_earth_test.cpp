// The floors of the seas (agent 51) beside those of the Earth, on the default world with its seas
// laid over the relief alone (as terrain_seas_stats --flags seas without a planet file), on a grid
// of 32 km: the bounds of the orchestrator at stop A. The mean depth 3,650 to 3,900 m (the Earth:
// 3,686 m from ETOPO, 3,795 m from Kossinna); at least 13 % of the sea from 200 m to 3 km (the
// Earth: about 15 %); at most 38 % from 4 to 5 km; at least 0.6 % from 6 to 7 km; the open floor
// between 2,500 and 6,300 m from its 5th to its 95th percentile; trenches beyond 8,000 m, on at
// most 0.1 % of the sea; at least 5 % of shelf; continental slopes of 2 to 6 degrees and rises of
// 0.5 to 1 degree (medians of the profiles from the shores).

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/seas.hpp"
#include "terrain/io/sea_stats.hpp"
#include "terrain/storage/world_meta.hpp"

#include "test_assert.hpp"

#include <chrono>
#include <cstdio>

int main() {
    const auto start = std::chrono::steady_clock::now();
    const std::uint64_t seed = terrain::kDefaultWorldSeed;
    const terrain::Heightfield relief(seed);
    terrain::SeaParams p;
    p.enabled = true;
    const terrain::Seas seas(seed, relief, nullptr, p, 0);
    terrain::SeaStatsParams params;
    params.cellSize = 32'768.0;
    params.seas = &seas;
    const terrain::SeaStats s = terrain::measureSeas(relief, params);
    const double middle = s.bands[1] + s.bands[2] + s.bands[3];
    const double trenches =
        static_cast<double>(s.trenchCells) / static_cast<double>(s.seaCells > 0 ? s.seaCells : 1);
    std::printf("seed %llu, cells of 32 km: mean depth %.0f m, 0-200 m %.1f %%, 200 m-3 km %.1f %%, "
                "4-5 km %.1f %%, 6-7 km %.2f %%, open floor p5 %.0f m p95 %.0f m, deepest %.0f m, "
                "deeper than 8 km %.3f %%, slope %.2f deg, rise %.2f deg, in %.1f s\n",
                static_cast<unsigned long long>(seed), s.meanDepth, 100.0 * s.bands[0],
                100.0 * middle, 100.0 * s.bands[5], 100.0 * s.bands[7], s.openShallowDepth,
                s.openDeepDepth, s.deepest, 100.0 * trenches, s.slopeDegrees.median,
                s.riseDegrees.median,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    TEST_CHECK(s.meanDepth >= 3'650.0 && s.meanDepth <= 3'900.0);
    TEST_CHECK(middle >= 0.13);
    TEST_CHECK(s.bands[5] <= 0.38);
    TEST_CHECK(s.bands[7] >= 0.006);
    TEST_CHECK(s.openShallowDepth >= 2'500.0 && s.openDeepDepth <= 6'300.0);
    TEST_CHECK(s.deepest > 8'000.0 && trenches <= 0.001);
    TEST_CHECK(s.bands[0] >= 0.05);
    TEST_CHECK(s.slopeDegrees.median >= 2.0 && s.slopeDegrees.median <= 6.0);
    TEST_CHECK(s.riseDegrees.median >= 0.5 && s.riseDegrees.median <= 1.0);
    return test::result();
}

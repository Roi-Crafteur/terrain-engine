// deepLakeShare (relief_lakes.hpp): a share of the lake area, never growing with the depth; and
// the watch on the deep basins of the default world (phase 11R bis):
// at most kDeepBound of the lake area of its Planet level in lakes at least 500 m deep, measured
// with cells of 16 km to stay light (the tools measure 8 km).
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_build.hpp"
#include "terrain/io/relief_lakes.hpp"

#include "test_assert.hpp"

#include <cstdio>

namespace {

constexpr double kDeepBound = 0.25;

void testDefaultWorld() {
    const terrain::Heightfield field(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{});
    terrain::PlanetParams params;
    params.cellSize = 16'384.0;
    params.floorSamples = 1; // the centers of the cells, as when the watch was set (phase 11R bis)
    params.threads = 0;
    const terrain::PlanetLevel level(terrain::planet::buildPlanet(field, params), params);
    TEST_CHECK(!level.lakes().empty());
    const double deep = terrain::deepLakeShare(level);
    std::printf("default world, cells of 16 km: %zu lakes, %.1f%% of their area at least 500 m "
                "deep\n",
                level.lakes().size(), 100.0 * deep);
    const double shallow = terrain::deepLakeShare(level, 200.0f);
    TEST_CHECK(deep >= 0.0 && deep <= shallow && shallow <= terrain::deepLakeShare(level, 0.0f) &&
               terrain::deepLakeShare(level, 0.0f) <= 1.0);
    TEST_CHECK(deep <= kDeepBound);
}

} // namespace

int main() {
    testDefaultWorld();
    return test::result();
}

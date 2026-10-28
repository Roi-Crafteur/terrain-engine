// The detail of the far horizon over the water of the world (phase 11B): on the small planet with
// its hydrology, around points of its rivers, the point heights and the lifts that
// HorizonDetailSampler reads in its fine grid (Heightfield::heightsAt in rows of nodes, through the
// regions of the hydrology) are those of Heightfield::sampleAt, point by point, and of
// HorizonCrestSampler::fill on them, as the epochs compute them, bit for bit; its slopes are those
// of HorizonNormalSampler.
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/water/hydrology.hpp"
#include "terrain/streaming/horizon_detail.hpp"

#include "planet_golden_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

namespace {

using terrain::HorizonIndex;
using terrain::HorizonParams;
using terrain::HorizonRect;

terrain::Heightfield waterWorld() {
    const terrain::Heightfield relief(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                                      golden::kSmallPlanet);
    const int threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    auto planet = std::make_shared<const terrain::PlanetLevel>(
        terrain::planet::buildPlanet(relief, golden::smallPlanetParams(threads)),
        golden::smallPlanetParams(threads));
    auto hydrology =
        std::make_shared<const terrain::Hydrology>(terrain::kDefaultWorldSeed, relief, planet);
    return relief.withHydrology(hydrology);
}

// Levels of 64 cells up to 512 m: the small planet is 1 048 576 m from north to south.
HorizonParams smallLevels(int crestRatio) {
    HorizonParams params;
    params.levels = 7;
    params.cellsPerSide = 64;
    params.crestRatio = crestRatio;
    params.world = golden::kSmallPlanet;
    return params;
}

// The detail of 8 x 6 samples around (x, z) against sampleAt and each sampler alone; returns the
// samples that rise.
int compare(const terrain::Heightfield& field, const HorizonParams& params, int level, double x,
            double z) {
    const HorizonIndex c = terrain::canonicalCenter(params, level, x, z);
    const HorizonRect rect{c.x - 4, c.z - 3, c.x + 4, c.z + 3};
    const terrain::HorizonDetailSampler sampler(field, params, 4);
    const terrain::HorizonDetail detail = sampler.sample(level, rect);
    const terrain::HorizonNormalSampler normals(field, params, 4);
    TEST_CHECK(detail.slopes == normals.sample(level, rect));
    const int pitch = terrain::levelPitch(params, level);
    std::vector<float> ground;
    for (std::int64_t k = rect.z0 - 1; k <= rect.z1; ++k) {
        for (std::int64_t i = rect.x0 - 1; i <= rect.x1; ++i) {
            ground.push_back(field.sampleAt(terrain::samplePosition(i, pitch),
                                            terrain::samplePosition(k, pitch))
                                 .height);
        }
    }
    std::vector<float> lifts(static_cast<std::size_t>(rect.count()));
    terrain::HorizonCrestSampler(field, params).fill(level, rect, ground, lifts);
    TEST_CHECK(detail.lifts == lifts);
    int raised = 0;
    std::size_t n = 0;
    for (std::int64_t k = 0; k < rect.height(); ++k) {
        for (std::int64_t i = 0; i < rect.width(); ++i, ++n) {
            const auto at = static_cast<std::size_t>((k + 1) * (rect.width() + 2) + i + 1);
            TEST_CHECK(detail.points[n] == ground[at]);
            raised += lifts[n] > 0.0f ? 1 : 0;
        }
    }
    return raised;
}

} // namespace

int main() {
    const terrain::Heightfield field = waterWorld();
    const auto& segments = field.hydrology()->planet().rivers().segments;
    int raised = 0;
    int places = 0;
    for (std::size_t s = 0; s < segments.size() && places < 6; s += 37, ++places) {
        const terrain::RiverVertex& v = segments[s].vertices[segments[s].vertices.size() / 2];
        for (const int crestRatio : {2, 4}) {
            for (const int level : {1, 2, 3}) {
                raised += compare(field, smallLevels(crestRatio), level, v.x, v.z);
            }
        }
    }
    std::printf("detail over the water: %d places along the rivers, %d samples rise\n", places,
                raised);
    TEST_CHECK(places == 6 && raised > 0);
    return test::result();
}

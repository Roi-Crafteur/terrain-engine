// Places of the river graph for 11B: deltas and estuaries at the sea mouths, waterfalls only
// at the breaks of slope, gorges between high walls.
#include "terrain/generation/planet/planet_features.hpp"

#include "planet_test_support.hpp"
#include "test_assert.hpp"

#include <cmath>
#include <cstddef>
#include <vector>

namespace {

using planet_test::Terrain;
using terrain::RiverFeature;
using terrain::RiverFeatureKind;

constexpr double kCell = 8000.0;

// Thresholds scaled to the 0.001 m^3/s of the test rivers; the river channels of the tests are
// cut into high ground, so gorges only count in their own test.
terrain::PlanetFeatureParams params(bool gorges = false) {
    terrain::PlanetFeatureParams p;
    p.deltaMinDischarge = 5.0e-4;
    p.estuaryMinDischarge = 5.0e-4;
    p.fallMinDischarge = 1.0e-4;
    p.gorgeMinDischarge = 1.0e-4;
    p.gorgeMinDepth = gorges ? 300.0 : 1.0e9;
    return p;
}

// A river along row 3 from column 25 down to the sea at column 0 (sea floor seaFloor), its bed
// at bed(i) cut into ground of 3,000 m.
template <class Bed> Terrain riverTerrain(float seaFloor, Bed bed, float ground = 3000.0f) {
    Terrain t = planet_test::flatTerrain(30, 7, ground, kCell);
    for (int j = 0; j < 7; ++j) {
        t.at(0, j) = seaFloor;
    }
    for (int i = 1; i <= 25; ++i) {
        t.at(i, 3) = bed(i);
    }
    t.markOcean();
    return t;
}

std::vector<RiverFeature> featuresOf(const Terrain& t,
                                     const terrain::PlanetFeatureParams& p = params()) {
    const planet_test::Network n = planet_test::network(t, planet_test::sources(t, {{25, 3}}));
    return terrain::planet::findRiverFeatures(t.grid, t.height, t.ocean, n.graph, p);
}

std::size_t count(const std::vector<RiverFeature>& features, RiverFeatureKind kind) {
    std::size_t n = 0;
    for (const RiverFeature& f : features) {
        n += f.kind == kind ? 1u : 0u;
    }
    return n;
}

void testDeltaAndEstuary() {
    // 1 m per cell (1.25e-4) into a sea 50 m deep: a delta at the last river cell (1, 3).
    const Terrain gentle = riverTerrain(-50.0f, [](int i) { return static_cast<float>(i); });
    const std::vector<RiverFeature> delta = featuresOf(gentle);
    TEST_CHECK(delta.size() == 1 && delta[0].kind == RiverFeatureKind::Delta);
    TEST_CHECK(delta[0].segment == 0 && delta[0].firstVertex == 24);
    TEST_CHECK(delta[0].x == gentle.grid.centerX(1) &&
               std::abs(delta[0].magnitude - 1.0 / kCell) < 1e-9);
    TEST_CHECK(delta[0].discharge == static_cast<float>(terrain::planet::cubicMeters(1000)));
    // The same river into a deep sea, or a steep one into the shallow sea: estuaries.
    const Terrain deep = riverTerrain(-1000.0f, [](int i) { return static_cast<float>(i); });
    const std::vector<RiverFeature> deepMouth = featuresOf(deep);
    TEST_CHECK(deepMouth.size() == 1 && deepMouth[0].kind == RiverFeatureKind::Estuary);
    const Terrain steep = riverTerrain(-50.0f, [](int i) { return 20.0f * static_cast<float>(i); });
    TEST_CHECK(count(featuresOf(steep), RiverFeatureKind::Estuary) == 1);
    // Below the thresholds, nothing.
    terrain::PlanetFeatureParams strict = params();
    strict.deltaMinDischarge = strict.estuaryMinDischarge = 1.0;
    TEST_CHECK(featuresOf(gentle, strict).empty());
}

void testWaterfall() {
    // 2 m per cell with an escarpment of 302 m between columns 13 and 12. The source above it
    // has no known slope above: no fall on its first step.
    const Terrain cliff = riverTerrain(
        -50.0f, [](int i) { return 2.0f * static_cast<float>(i) + (i >= 13 ? 300.0f : 0.0f); });
    const std::vector<RiverFeature> features = featuresOf(cliff);
    TEST_CHECK(count(features, RiverFeatureKind::Waterfall) == 1);
    for (const RiverFeature& f : features) {
        if (f.kind == RiverFeatureKind::Waterfall) {
            TEST_CHECK(f.segment == 0 && f.firstVertex == 12 && f.magnitude == 302.0f);
            TEST_CHECK(f.x == 0.5 * (cliff.grid.centerX(13) + cliff.grid.centerX(12)));
        }
    }
    // A river steep all along has no break of slope: no waterfall.
    const Terrain steep =
        riverTerrain(-50.0f, [](int i) { return 100.0f * static_cast<float>(i); });
    TEST_CHECK(count(featuresOf(steep), RiverFeatureKind::Waterfall) == 0);
}

void testGorge() {
    // Lowland at 100 m with walls of 600 m on both sides of the river from column 10 to 15.
    Terrain t = riverTerrain(-50.0f, [](int i) { return 2.0f * static_cast<float>(i); }, 100.0f);
    for (int i = 10; i <= 15; ++i) {
        for (const int j : {1, 2, 4, 5}) {
            t.at(i, j) = 600.0f;
        }
    }
    t.markOcean();
    const std::vector<RiverFeature> features = featuresOf(t, params(true));
    TEST_CHECK(count(features, RiverFeatureKind::Gorge) == 1);
    for (const RiverFeature& f : features) {
        if (f.kind == RiverFeatureKind::Gorge) {
            // Columns 15 down to 10 are points 10 to 15; deepest at column 10 (bed at 20 m).
            TEST_CHECK(f.firstVertex == 10 && f.lastVertex == 15 && f.magnitude == 580.0f);
            TEST_CHECK(f.x == t.grid.centerX(10));
        }
    }
}

} // namespace

int main() {
    testDeltaAndEstuary();
    testWaterfall();
    testGorge();
    return test::result();
}

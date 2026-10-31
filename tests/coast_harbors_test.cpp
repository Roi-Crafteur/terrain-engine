// The harbors of the coasts (agent 33, Heightfield::harborsIn) on the default world, and on the
// small world with its planet level and its rivers:
// - harbors found near coasts of every kind, each a sheltered inlet: its mouth at least
//   kHarborOpening wide and narrower than its water inside, its floor at least kHarborDepth deep
//   at its site (the sea there, the ground well below it), its shelter at least kHarborShelter;
//   the harbors exactly the inlets flagged so;
// - the same sites whatever the boxes: a box is the union of its quarters, and two overlapping
//   boxes give the same sites where they overlap;
// - the same sites whatever the threads and the order of the queries;
// - with a Hydrology, every site clear of the valleys, ponds and lakes of the rivers.
#include "terrain/generation/coast.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_build.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include "planet_golden_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <thread>
#include <tuple>
#include <vector>

namespace {

using terrain::CoastSite;
using terrain::Heightfield;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;
constexpr double kHalfBox = 100'000.0;

// Coasts of cliffs, fjords, calanques and rias, the boxes of the test around them.
constexpr std::array<std::array<double, 2>, 4> kCoasts{{{-26'486'937.0, 7'219'391.0},
                                                        {1'793'548.0, 9'821'452.0},
                                                        {23'433'101.0, -2'128'828.0},
                                                        {-15'720'496.0, -6'266'783.0}}};

bool same(const CoastSite& a, const CoastSite& b) {
    return std::tie(a.x, a.z, a.opening, a.size, a.length, a.depth, a.shelter, a.inlet, a.coast,
                    a.harbor) == std::tie(b.x, b.z, b.opening, b.size, b.length, b.depth,
                                          b.shelter, b.inlet, b.coast, b.harbor);
}

bool sameSites(const std::vector<CoastSite>& a, const std::vector<CoastSite>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), same);
}

std::vector<CoastSite> sitesAround(const Heightfield& f, const std::array<double, 2>& c) {
    return f.inletsIn(c[0] - kHalfBox, c[1] - kHalfBox, c[0] + kHalfBox, c[1] + kHalfBox);
}

void testHarbors(const Heightfield& f) {
    int harbors = 0;
    int inlets = 0;
    bool sheltered = true;
    bool flagged = true;
    bool sea = true;
    for (const auto& c : kCoasts) {
        const std::vector<CoastSite> all = sitesAround(f, c);
        std::vector<CoastSite> expected;
        for (const CoastSite& s : all) {
            const bool harbor = s.shelter >= terrain::kHarborShelter &&
                                s.depth >= terrain::kHarborDepth &&
                                s.opening >= terrain::kHarborOpening && s.opening < s.size;
            flagged = flagged && harbor == s.harbor;
            if (s.harbor) {
                expected.push_back(s);
            }
        }
        const std::vector<CoastSite> found =
            f.harborsIn(c[0] - kHalfBox, c[1] - kHalfBox, c[0] + kHalfBox, c[1] + kHalfBox);
        flagged = flagged && sameSites(found, expected);
        for (const CoastSite& s : found) {
            sheltered = sheltered && s.opening < s.size && s.depth >= terrain::kHarborDepth;
            const terrain::HeightSample h = f.sampleAt(s.x, s.z);
            sea = sea && f.coastAt(s.x, s.z).signal < 0.0 &&
                  h.water == terrain::WaterKind::Ocean && h.height < -0.5f * s.depth;
        }
        std::printf("around (%.0f, %.0f): %zu inlets, %zu harbors\n", c[0], c[1], all.size(),
                    found.size());
        harbors += static_cast<int>(found.size());
        inlets += static_cast<int>(all.size());
    }
    TEST_CHECK(harbors >= 20 && harbors < inlets);
    TEST_CHECK(flagged);
    TEST_CHECK(sheltered);
    TEST_CHECK(sea);
}

// A box is the union of its quarters; overlapping boxes agree on their overlap.
void testBoxes(const Heightfield& f) {
    bool unions = true;
    bool overlaps = true;
    for (const auto& c : kCoasts) {
        const std::vector<CoastSite> whole = sitesAround(f, c);
        std::vector<CoastSite> quarters;
        for (const double dz : {-kHalfBox, 0.0}) {
            for (const double dx : {-kHalfBox, 0.0}) {
                const std::vector<CoastSite> q =
                    f.inletsIn(c[0] + dx, c[1] + dz, c[0] + dx + kHalfBox, c[1] + dz + kHalfBox);
                quarters.insert(quarters.end(), q.begin(), q.end());
            }
        }
        std::sort(quarters.begin(), quarters.end(), [](const CoastSite& a, const CoastSite& b) {
            return std::tie(a.x, a.z, a.inlet, a.length, a.opening, a.size, a.depth, a.shelter,
                            a.coast) < std::tie(b.x, b.z, b.inlet, b.length, b.opening, b.size,
                                                b.depth, b.shelter, b.coast);
        });
        unions = unions && sameSites(whole, quarters);
        const std::vector<CoastSite> shifted =
            f.inletsIn(c[0], c[1] - kHalfBox, c[0] + 2.0 * kHalfBox, c[1] + kHalfBox);
        std::vector<CoastSite> a;
        std::vector<CoastSite> b;
        std::copy_if(whole.begin(), whole.end(), std::back_inserter(a),
                     [&c](const CoastSite& s) { return s.x >= c[0]; });
        std::copy_if(shifted.begin(), shifted.end(), std::back_inserter(b),
                     [&c](const CoastSite& s) { return s.x < c[0] + kHalfBox; });
        overlaps = overlaps && sameSites(a, b);
    }
    TEST_CHECK(unions);
    TEST_CHECK(overlaps);
}

// Four threads over fresh Heightfields, the coasts in another order each: the same sites.
void testThreads(const Heightfield& f) {
    std::array<std::array<std::vector<CoastSite>, 4>, 4> got;
    std::vector<std::thread> threads;
    for (std::size_t t = 0; t < 4; ++t) {
        threads.emplace_back([&got, t] {
            const Heightfield own(kSeed);
            for (std::size_t k = 0; k < kCoasts.size(); ++k) {
                const std::size_t c = (k + t) % kCoasts.size();
                got[t][c] = sitesAround(own, kCoasts[c]);
            }
        });
    }
    for (std::thread& t : threads) {
        t.join();
    }
    bool same = true;
    for (std::size_t c = 0; c < kCoasts.size(); ++c) {
        const std::vector<CoastSite> expected = sitesAround(f, kCoasts[c]);
        for (std::size_t t = 0; t < 4; ++t) {
            same = same && sameSites(got[t][c], expected);
        }
    }
    TEST_CHECK(same);
}

// With the rivers of the small world, every site at least 100 m clear of their valleys, ponds
// and lakes (the inlets stop that far from them).
void testRivers() {
    const Heightfield relief(kSeed, terrain::HeightfieldParams{}, golden::kSmallPlanet);
    const int threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    auto planet = std::make_shared<const terrain::PlanetLevel>(
        terrain::planet::buildPlanet(relief, golden::smallPlanetParams(threads)),
        golden::smallPlanetParams(threads));
    auto hydrology = std::make_shared<const terrain::Hydrology>(kSeed, relief, planet);
    const Heightfield world = relief.withHydrology(hydrology);
    const double half = 0.5 * golden::kSmallPlanet.x;
    const std::vector<CoastSite> sites =
        world.inletsIn(-half, -0.5 * golden::kSmallPlanet.z, half, 0.5 * golden::kSmallPlanet.z);
    const std::vector<CoastSite> dry =
        relief.inletsIn(-half, -0.5 * golden::kSmallPlanet.z, half, 0.5 * golden::kSmallPlanet.z);
    double least = 1e9;
    for (const CoastSite& s : sites) {
        least = std::min(least, hydrology->clearance(s.x, s.z,
                                                     static_cast<double>(
                                                         hydrology->relief().heightAt(s.x, s.z))));
    }
    std::printf("small world: %zu inlets with its rivers, %zu without them; their sites %.0f m "
                "clear of the rivers at least\n",
                sites.size(), dry.size(), least);
    TEST_CHECK(!sites.empty() && sites.size() <= dry.size());
    TEST_CHECK(least >= 100.0);
}

} // namespace

int main() {
    const Heightfield f(kSeed);
    testHarbors(f);
    testBoxes(f);
    testThreads(f);
    testRivers();
    return test::result();
}

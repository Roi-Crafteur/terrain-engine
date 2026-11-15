// The marginal plateaus of the seas (agent 51, sea_plateaus.hpp): on the default world, each at
// sea on a passive margin at its distance from the large lands; of every size, spacing, stretch,
// outline and depth (pattern_support: their coefficients of variation), in no row (no peak of
// the autocorrelation of their relief along the parallels, beyond the width of one plateau); a
// mesa whose top is never shallower than its least depth and whose flanks fall away; and the same
// plateaus whatever the threads of the plates and the shores they stand on.

#include "sea_plateaus.hpp"
#include "sea_plates.hpp"
#include "sea_shore.hpp"

#include "dunes_support.hpp"
#include "pattern_support.hpp"
#include "test_assert.hpp"

#include "terrain/generation/geography.hpp"
#include "terrain/generation/heightfield.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace {

namespace sp = terrain::sea_plateaus;

terrain::SeaParams params() {
    terrain::SeaParams p;
    p.enabled = true;
    return p;
}

sp::Plateaus plateausOf(const terrain::Heightfield& relief, std::uint64_t seed, int threads,
                        const terrain::SeaParams& p,
                        terrain::sea_plates::Plates* platesOut = nullptr,
                        terrain::sea_shore::Shore* shoreOut = nullptr) {
    terrain::sea_plates::Plates plates =
        terrain::sea_plates::build(seed, *relief.geography(), threads);
    terrain::sea_shore::Shore shore =
        terrain::sea_shore::build(*relief.geography(), p.islandArea, threads);
    sp::Plateaus out = sp::build(seed, relief.world(), p, shore, plates);
    if (platesOut != nullptr) {
        *platesOut = std::move(plates);
    }
    if (shoreOut != nullptr) {
        *shoreOut = std::move(shore);
    }
    return out;
}

void testPlaced(const sp::Plateaus& s, const terrain::sea_plates::Plates& plates,
                const terrain::sea_shore::Shore& shore) {
    const terrain::SeaParams p = params();
    std::size_t wrong = 0;
    for (const sp::Plateau& q : s.list) {
        const terrain::sea_shore::Distances d = terrain::sea_shore::distancesAt(shore, q.x, q.z);
        const double active = terrain::sea_plates::splineAt(plates, plates.active, q.x, q.z);
        const bool placed = d.any >= 20'000.0 && d.big >= p.marginalNear &&
                            d.big <= p.marginalFar && active < static_cast<double>(p.activeLow);
        const bool shaped = q.radius >= p.marginalRadiusLow && q.radius <= p.marginalRadiusHigh &&
                            q.stretch >= 1.0 && q.stretch <= p.marginalStretch &&
                            q.top >= p.marginalTopLow && q.top <= p.marginalTopHigh &&
                            std::abs(q.seaX * q.seaX + q.seaZ * q.seaZ - 1.0) < 1e-9;
        wrong += placed && shaped ? 0 : 1;
    }
    std::printf("default world: %zu marginal plateaus, %zu misplaced or misshaped\n",
                s.list.size(), wrong);
    TEST_CHECK(s.list.size() >= 100);
    TEST_CHECK(wrong == 0);
}

double torusDistance(const sp::Plateau& a, const sp::Plateau& b, const terrain::WorldSize& w) {
    return std::hypot(terrain::wrapCoordinate(a.x - b.x, w.x),
                      terrain::wrapCoordinate(a.z - b.z, w.z));
}

void testVaried(const sp::Plateaus& s) {
    std::vector<double> radii, spacings, stretches, bends, tops;
    for (std::size_t i = 0; i < s.list.size(); ++i) {
        const sp::Plateau& q = s.list[i];
        radii.push_back(q.radius);
        stretches.push_back(q.stretch);
        bends.push_back(q.bend[0] + q.bend[1] + q.bend[2]);
        tops.push_back(q.top);
        double nearest = std::numeric_limits<double>::infinity();
        for (std::size_t j = 0; j < s.list.size(); ++j) {
            if (j != i) {
                nearest = std::min(nearest, torusDistance(q, s.list[j], s.world));
            }
        }
        spacings.push_back(nearest);
    }
    const auto [least, most] = std::minmax_element(stretches.begin(), stretches.end());
    std::printf("variation: radii %.2f, nearest neighbors %.2f, bends %.2f, tops %.2f; stretch "
                "%.2f to %.2f\n",
                pattern_test::variation(radii), pattern_test::variation(spacings),
                pattern_test::variation(bends), pattern_test::variation(tops), *least, *most);
    TEST_CHECK(pattern_test::variation(radii) >= 0.3);
    TEST_CHECK(pattern_test::variation(spacings) >= 0.3);
    TEST_CHECK(pattern_test::variation(bends) >= 0.2);
    TEST_CHECK(pattern_test::variation(tops) >= 0.3);
    TEST_CHECK(*least < 1.3 && *most > 2.0);
}

// Their relief over 6 km along the parallels of the middles of some plateaus, every 16 km around
// the world: no peak of the autocorrelation beyond 1,100 km (wider than any one plateau above
// 6 km), where a row of plateaus at one spacing would stand out.
void testNoRow(const sp::Plateaus& s) {
    constexpr double kStep = 16'384.0;
    double worst = -1.0;
    for (std::size_t k = 0; k < s.list.size(); k += std::max<std::size_t>(1, s.list.size() / 12)) {
        std::vector<double> profile;
        for (double x = -0.5 * s.world.x; x < 0.5 * s.world.x; x += kStep) {
            profile.push_back(std::max(0.0, sp::elevationAt(s, x, s.list[k].z) + 6'000.0));
        }
        worst = std::max(worst, pattern_test::peakCorrelation(profile, 68, 400));
    }
    std::printf("the largest autocorrelation along the parallels, lags 1,100 to 6,500 km: %.3f\n",
                worst);
    TEST_CHECK(worst < 0.5);
}

// A mesa: its top at the middle, never shallower than its least depth anywhere, its flanks falling
// away along every ray, nothing beyond its reach.
void testMesa(const sp::Plateaus& s) {
    std::size_t wrong = 0;
    for (std::size_t k = 0; k < s.list.size(); k += 7) {
        const sp::Plateau& q = s.list[k];
        const double middle = sp::elevationOf(q, s.world, q.x, q.z);
        wrong += std::abs(middle + std::max(q.shallowest, q.top)) < 1.0 ? 0 : 1;
        for (int ray = 0; ray < 8; ++ray) {
            const double angle = 0.785398163 * ray;
            double last = middle;
            for (double r = 0.0; r < q.reach; r += 10'000.0) {
                const double x = terrain::wrapCoordinate(q.x + r * std::cos(angle), s.world.x);
                const double z = terrain::wrapCoordinate(q.z + r * std::sin(angle), s.world.z);
                const double e = sp::elevationOf(q, s.world, x, z);
                wrong += e <= -q.shallowest + 1e-9 ? 0 : 1;
                // Beyond the outline (bent at most 1 + bends), ever lower.
                const double out = q.radius * q.stretch * (1.0 + q.bend[0] + q.bend[1] + q.bend[2]);
                wrong += r > out + 10'000.0 && e > last + 1e-9 ? 1 : 0;
                last = e;
            }
            const double x = terrain::wrapCoordinate(q.x + 1.01 * q.reach, s.world.x);
            wrong += sp::elevationOf(q, s.world, x, q.z) == sp::kNone ? 0 : 1;
        }
    }
    std::printf("mesas: %zu points off their shape\n", wrong);
    TEST_CHECK(wrong == 0);
}

bool same(const sp::Plateaus& a, const sp::Plateaus& b) {
    return a.list.size() == b.list.size() && a.members == b.members && a.start == b.start &&
           (a.list.empty() ||
            std::memcmp(a.list.data(), b.list.data(), a.list.size() * sizeof(sp::Plateau)) == 0);
}

} // namespace

int main() {
    {
        // The small world has narrow seas: plateaus nearer its shores, in more cells.
        terrain::SeaParams p = params();
        p.marginalShare = 0.6;
        p.marginalNear = 30'000.0;
        p.marginalFar = 400'000.0;
        const terrain::Heightfield& small = dunes_test::small().relief;
        const sp::Plateaus one = plateausOf(small, dunes_test::kSeed, 1, p);
        const sp::Plateaus four = plateausOf(small, dunes_test::kSeed, 4, p);
        std::printf("small world: %zu marginal plateaus\n", one.list.size());
        TEST_CHECK(!one.list.empty() && same(one, four));
    }
    const auto start = std::chrono::steady_clock::now();
    const terrain::Heightfield relief(dunes_test::kSeed);
    terrain::sea_plates::Plates plates;
    terrain::sea_shore::Shore shore;
    const sp::Plateaus s = plateausOf(relief, dunes_test::kSeed, 0, params(), &plates, &shore);
    std::printf("default world built in %.1f s, %zu bytes\n",
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(),
                s.memoryBytes());
    testPlaced(s, plates, shore);
    testVaried(s);
    testNoRow(s);
    testMesa(s);
    return test::result();
}

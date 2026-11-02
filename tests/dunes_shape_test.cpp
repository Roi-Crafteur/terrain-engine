// The forms of the dunes at their real scale (agent 34, ergs.hpp), on the default world:
// transverse ridges 20 to 300 m high, their crests 1 to 3 km apart, their slip faces at the angle
// of repose (about 33 degrees) and their backs gentle; barchans apart on a bare floor; seifs along
// the wind; stars. No regular pattern: the spacings vary. Far from the origin (+-20,000 km), across
// the boundary of a cell of the planet level where the wind turns, the spacing of the crests holds.
#include "dunes_support.hpp"
#include "test_assert.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using dunes_test::real;

// Sites of the default world, far from the origin too, in the trades and near 30 degrees.
struct Site {
    double x;
    double z;
};
const std::vector<Site> kSites = {
    {0.0, 3'000'000.0},          {12'345'678.0, -4'000'000.0}, {20'000'000.0, -5'500'000.0},
    {-20'000'000.0, 2'600'000.0}, {-7'654'321.0, 5'000'000.0}, {19'999'744.0, 5'830'000.0},
};

constexpr float kChainSand = 0.6f;
constexpr float kSteady = 0.1f;

// The chains of the realistic ergs (the Grand Erg Oriental: decision 3 of agent 34's third
// stop, no transverse ridges any more): linear ridges along the wind, 1 to 3.5 km apart across
// it, one flank a slip face at the angle of repose (31 to 35 degrees) where the chain is fully
// formed (over 0.6 of the tallest: its ends and its defects fade it, faces and all), the other
// flank gentler (22 degrees, at most 28 with the plinth and the bends), up to 300 m high, their spacings irregular.
void testChains() {
    std::vector<double> all;
    std::vector<std::array<double, 3>> flanks; // height, slip face, gentle flank
    double highest = 0.0;
    for (const Site& s : kSites) {
        const std::array<double, 2> w = real().ergs.windAt(s.x, s.z);
        const std::vector<double> crests = dunes_test::crestsAlong(
            real().ergs, s.x, s.z, -w[1], w[0], 24'000.0, 5.0, kChainSand, kSteady, 20.0);
        const std::vector<double> gaps = dunes_test::spacings(crests);
        all.insert(all.end(), gaps.begin(), gaps.end());
        for (const double c : crests) {
            const double x = s.x - c * w[1];
            const double z = s.z + c * w[0];
            const double h = dunes_test::forcedDunes(real().ergs, x, z, kChainSand, kSteady);
            highest = std::max(highest, h);
            // 12 m off the crest on each side: the slip face and the gentle flank.
            const double a = dunes_test::slopeAt(real().ergs, x - 12.0 * w[1], z + 12.0 * w[0],
                                                 kChainSand, kSteady);
            const double b = dunes_test::slopeAt(real().ergs, x + 12.0 * w[1], z - 12.0 * w[0],
                                                 kChainSand, kSteady);
            flanks.push_back({h, std::max(a, b), std::min(a, b)});
        }
    }
    std::vector<double> lee;
    std::vector<double> gentle;
    for (const std::array<double, 3>& f : flanks) {
        if (f[0] >= 0.6 * highest) {
            lee.push_back(f[1]);
            gentle.push_back(f[2]);
        }
    }
    const double median = dunes_test::quantile(all, 0.5);
    double mean = 0.0;
    for (const double g : all) {
        mean += g / static_cast<double>(all.size());
    }
    double var = 0.0;
    for (const double g : all) {
        var += (g - mean) * (g - mean) / static_cast<double>(all.size());
    }
    std::printf("chains: %zu spacings, median %.0f m (p10 %.0f, p90 %.0f), highest %.0f m, slip "
                "faces (%zu formed) p25 %.1f p50 %.1f p75 %.1f deg, gentle flanks p50 %.1f p95 %.1f deg, "
                "spacing variation %.2f\n",
                all.size(), median, dunes_test::quantile(all, 0.1), dunes_test::quantile(all, 0.9),
                highest, lee.size(), dunes_test::quantile(lee, 0.25), dunes_test::quantile(lee, 0.5),
                dunes_test::quantile(lee, 0.75), dunes_test::quantile(gentle, 0.5),
                dunes_test::quantile(gentle, 0.95), std::sqrt(var) / mean);
    TEST_CHECK(all.size() >= 40 && lee.size() >= 20);
    TEST_CHECK(median >= 1000.0 && median <= 3500.0);
    TEST_CHECK(highest >= 20.0 && highest <= 300.0);
    TEST_CHECK(dunes_test::quantile(lee, 0.5) >= 31.0 && dunes_test::quantile(lee, 0.5) <= 35.0);
    TEST_CHECK(dunes_test::quantile(gentle, 0.95) <= 28.0);
    TEST_CHECK(std::sqrt(var) / mean >= 0.1);
}

// The sand of the view of the straight band (624 154, 3 340 470, heading 180, 300 m up).
constexpr float kRibbonSand = 0.4f;

// No crest runs on for tens of kilometers (from 300 m up, looking along one, a straight band to
// the horizon): followed along the wind, a chain ends, breaks or joins another within a few
// kilometers. 20 km along the chains, crests over half the height.
void testCrestSegments() {
    std::vector<double> lengths;
    for (const Site& s : kSites) {
        const std::array<double, 2> w = real().ergs.windAt(s.x, s.z);
        constexpr int kAlong = 400; // along the chains (the wind), every 50 m
        constexpr int kAcross = 300; // across them, every 20 m
        std::vector<double> h(static_cast<std::size_t>(kAlong * kAcross));
        double top = 0.0;
        for (int t = 0; t < kAlong; ++t) {
            for (int u = 0; u < kAcross; ++u) {
                const double x = s.x + 50.0 * t * w[0] - 20.0 * u * w[1];
                const double z = s.z + 50.0 * t * w[1] + 20.0 * u * w[0];
                const double v = dunes_test::forcedDunes(real().ergs, x, z, kRibbonSand, kSteady);
                h[static_cast<std::size_t>(t * kAcross + u)] = v;
                top = std::max(top, v);
            }
        }
        const auto crest = [&](int t, int u) {
            const double* r = &h[static_cast<std::size_t>(t * kAcross)];
            return u > 0 && u + 1 < kAcross && r[u] > 0.5 * top && r[u] >= r[u - 1] && r[u] > r[u + 1];
        };
        for (int start = 0; start < kAlong; start += 25) {
            for (int u0 = 1; u0 + 1 < kAcross; ++u0) {
                if (!crest(start, u0)) {
                    continue;
                }
                int u = u0;
                int t = start;
                while (t + 1 < kAlong) {
                    int next = -1;
                    for (int du = -3; du <= 3 && next < 0; ++du) {
                        next = crest(t + 1, u + du) ? u + du : -1;
                    }
                    if (next < 0) {
                        break;
                    }
                    u = next;
                    ++t;
                }
                lengths.push_back(0.05 * (t - start + 1));
            }
        }
    }
    // A crest followed through a junction goes on along the other: the longest may reach the end
    // of the window; most do not.
    const double p90 = dunes_test::quantile(lengths, 0.9);
    const double p95 = dunes_test::quantile(lengths, 0.95);
    std::printf("crests of the chains: %zu followed, median %.1f km, p90 %.1f km, p95 %.1f km\n",
                lengths.size(), dunes_test::quantile(lengths, 0.5), p90, p95);
    TEST_CHECK(lengths.size() >= 50);
    TEST_CHECK(p90 <= 10.0 && p95 <= 12.0);
}

// Barchans where the sand is scarcest (alone under 0.02 of sand, mixed with the chains above):
// crescents apart, the floor bare between them (more than half of the ground), at most 60 m high.
void testBarchans() {
    std::size_t bare = 0;
    std::size_t count = 0;
    double highest = 0.0;
    for (const Site& s : kSites) {
        for (int j = 0; j < 60; ++j) {
            for (int i = 0; i < 60; ++i) {
                const double h = dunes_test::forcedDunes(real().ergs, s.x + 50.0 * i, s.z + 50.0 * j,
                                                         0.0f, kSteady);
                bare += h == 0.0 ? 1 : 0;
                ++count;
                highest = std::max(highest, h);
            }
        }
    }
    const double share = static_cast<double>(bare) / static_cast<double>(count);
    std::printf("barchans: bare floor %.0f %%, highest %.0f m\n", 100.0 * share, highest);
    TEST_CHECK(share >= 0.5 && share < 0.98);
    TEST_CHECK(highest >= 10.0 && highest <= 60.0);
}

// Seifs where the winds come from two sides: crests along the wind, 1 to 3.5 km apart across it.
void testSeifs() {
    std::vector<double> all;
    std::vector<double> along;
    for (const Site& s : kSites) {
        const std::array<double, 2> w = real().ergs.windAt(s.x, s.z);
        const std::vector<double> crests = dunes_test::crestsAlong(
            real().ergs, s.x, s.z, -w[1], w[0], 24'000.0, 5.0, 0.6f, 0.5f, 20.0);
        const std::vector<double> gaps = dunes_test::spacings(crests);
        all.insert(all.end(), gaps.begin(), gaps.end());
        // Along the wind, a seif crosses no crest: few maxima.
        const std::vector<double> lengthwise = dunes_test::crestsAlong(
            real().ergs, s.x, s.z, w[0], w[1], 24'000.0, 5.0, 0.6f, 0.5f, 20.0);
        along.push_back(static_cast<double>(lengthwise.size()));
    }
    const double median = dunes_test::quantile(all, 0.5);
    std::printf("seifs: %zu spacings across the wind, median %.0f m; crests met along it, median "
                "%.0f per 24 km\n",
                all.size(), median, dunes_test::quantile(along, 0.5));
    TEST_CHECK(median >= 1000.0 && median <= 3500.0);
    TEST_CHECK(dunes_test::quantile(along, 0.5) <
               static_cast<double>(all.size()) / static_cast<double>(kSites.size()));
}

// Stars where the winds come from every side: peaks of 3 to 5 arms, at most 300 m.
void testStars() {
    double highest = 0.0;
    std::size_t peaks = 0;
    const Site s = kSites[1];
    std::vector<double> h(200 * 200);
    for (int j = 0; j < 200; ++j) {
        for (int i = 0; i < 200; ++i) {
            h[static_cast<std::size_t>(j * 200 + i)] =
                dunes_test::forcedDunes(real().ergs, s.x + 50.0 * i, s.z + 50.0 * j, 0.7f, 0.9f);
        }
    }
    for (int j = 1; j < 199; ++j) {
        for (int i = 1; i < 199; ++i) {
            const double c = h[static_cast<std::size_t>(j * 200 + i)];
            highest = std::max(highest, c);
            bool top = c > 50.0;
            for (int d = 0; d < 9 && top; ++d) {
                top = c >= h[static_cast<std::size_t>((j + d / 3 - 1) * 200 + i + d % 3 - 1)];
            }
            peaks += top ? 1 : 0;
        }
    }
    std::printf("stars: %zu peaks over 100 km2, highest %.0f m\n", peaks, highest);
    TEST_CHECK(peaks >= 5 && peaks <= 60);
    TEST_CHECK(highest > 50.0 && highest <= 300.0);
}

// Across the boundaries that cut the world (the cells of the planet level, 65,536 m here, the
// lattice of the pivots of the phase, 16,384 m) and its seam, far from the origin: no step, no
// seam, whatever the kind of the realistic ergs (the chains space too irregularly for their
// spacings on each side to be compared).
void testBoundaries() {
    double steepest = 0.0;
    const double period = real().ergs.grid().periodX();
    for (const double z : {-5'830'000.0, 3'000'000.0, 5'830'000.0}) {
        for (const double b : {std::floor(19'990'000.0 / 65'536.0) * 65'536.0,
                               std::floor(-12'345'678.0 / 16'384.0) * 16'384.0,
                               0.5 * period}) {
            for (const auto& kind : {std::array<float, 2>{0.0f, kSteady}, {0.2f, kSteady},
                                     {kChainSand, kSteady}, {0.7f, 0.9f}}) {
                double last = dunes_test::forcedDunes(real().ergs, b - 50.0, z, kind[0], kind[1]);
                for (double d = -49.75; d <= 50.0; d += 0.25) {
                    const double h = dunes_test::forcedDunes(real().ergs, b + d, z, kind[0], kind[1]);
                    steepest = std::max(steepest, std::abs(h - last) / 0.25);
                    last = h;
                }
            }
        }
    }
    std::printf("boundaries: steepest step across them %.2f m per m (%.1f deg)\n", steepest,
                std::atan(steepest) * dunes_test::kDegrees);
    TEST_CHECK(std::atan(steepest) * dunes_test::kDegrees <= 40.0);
}

// Continuous: no step anywhere, whatever the kind (the steepest pair of points 0.25 m apart).
void testContinuous() {
    double steepest = 0.0;
    for (const Site& s : kSites) {
        for (const auto& kind : {std::array<float, 2>{0.1f, kSteady}, {0.6f, kSteady},
                                 {0.95f, kSteady}, {0.6f, 0.5f}, {0.7f, 0.9f}, {0.6f, 0.6f}}) {
            for (int k = 0; k < 4000; ++k) {
                const double x = s.x + 7.31 * k;
                const double z = s.z + 3.17 * k;
                const double a = dunes_test::forcedDunes(real().ergs, x, z, kind[0], kind[1]);
                const double b = dunes_test::forcedDunes(real().ergs, x + 0.25, z, kind[0], kind[1]);
                steepest = std::max(steepest, std::abs(b - a) / 0.25);
            }
        }
    }
    std::printf("steepest step: %.2f m per m (%.1f deg)\n", steepest,
                std::atan(steepest) * dunes_test::kDegrees);
    TEST_CHECK(std::atan(steepest) * dunes_test::kDegrees <= 40.0);
}

} // namespace

int main() {
    testChains();
    testCrestSegments();
    testBarchans();
    testSeifs();
    testStars();
    testBoundaries();
    testContinuous();
    return test::result();
}

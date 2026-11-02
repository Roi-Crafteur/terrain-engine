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

constexpr float kTransverseSand = 0.6f;
constexpr float kSteady = 0.1f;

void testTransverse() {
    std::vector<double> all;
    std::vector<double> lee;
    std::vector<double> back;
    double highest = 0.0;
    for (const Site& s : kSites) {
        const std::array<double, 2> w = real().ergs.windAt(s.x, s.z);
        const std::vector<double> crests = dunes_test::crestsAlong(
            real().ergs, s.x, s.z, w[0], w[1], 24'000.0, 5.0, kTransverseSand, kSteady, 20.0);
        const std::vector<double> gaps = dunes_test::spacings(crests);
        all.insert(all.end(), gaps.begin(), gaps.end());
        for (const double c : crests) {
            const double x = s.x + c * w[0];
            const double z = s.z + c * w[1];
            highest = std::max(highest, dunes_test::forcedDunes(real().ergs, x, z, kTransverseSand, kSteady));
            // Just downwind of the crest: the slip face; upwind, half way down: the back.
            lee.push_back(dunes_test::slopeAt(real().ergs, x + 12.0 * w[0], z + 12.0 * w[1],
                                              kTransverseSand, kSteady));
            back.push_back(dunes_test::slopeAt(real().ergs, x - 300.0 * w[0], z - 300.0 * w[1],
                                               kTransverseSand, kSteady));
        }
    }
    const double median = dunes_test::quantile(all, 0.5);
    std::printf("transverse: %zu spacings, median %.0f m (p10 %.0f, p90 %.0f), highest %.0f m, "
                "slip faces p25 %.1f p50 %.1f p75 %.1f deg, backs p50 %.1f p95 %.1f deg\n",
                all.size(), median, dunes_test::quantile(all, 0.1), dunes_test::quantile(all, 0.9),
                highest, dunes_test::quantile(lee, 0.25), dunes_test::quantile(lee, 0.5),
                dunes_test::quantile(lee, 0.75), dunes_test::quantile(back, 0.5),
                dunes_test::quantile(back, 0.95));
    TEST_CHECK(all.size() >= 40);
    TEST_CHECK(median >= 1000.0 && median <= 3000.0);
    TEST_CHECK(highest >= 20.0 && highest <= 300.0);
    TEST_CHECK(dunes_test::quantile(lee, 0.5) >= 31.0 && dunes_test::quantile(lee, 0.5) <= 35.0);
    TEST_CHECK(dunes_test::quantile(back, 0.95) <= 13.0);
    // No regular pattern: the spacings vary by more than a tenth.
    double mean = 0.0;
    for (const double g : all) {
        mean += g / static_cast<double>(all.size());
    }
    double var = 0.0;
    for (const double g : all) {
        var += (g - mean) * (g - mean) / static_cast<double>(all.size());
    }
    std::printf("  spacing variation %.2f\n", std::sqrt(var) / mean);
    TEST_CHECK(std::sqrt(var) / mean >= 0.1);
}

// Barchans where the sand is scarce: crescents apart, the floor bare between them (more than
// half of the ground), at most 60 m high.
void testBarchans() {
    std::size_t bare = 0;
    std::size_t count = 0;
    double highest = 0.0;
    for (const Site& s : kSites) {
        for (int j = 0; j < 60; ++j) {
            for (int i = 0; i < 60; ++i) {
                const double h = dunes_test::forcedDunes(real().ergs, s.x + 50.0 * i, s.z + 50.0 * j,
                                                         0.1f, kSteady);
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

// Far from the origin, across the boundary of a cell of the planet level (8,192 m) where the wind
// turns fast (near 30 degrees), the spacing of the crests holds: within 1.5 times on each side.
void testFarBoundary() {
    double worst = 1.0;
    for (const double x0 : {20'000'000.0 - 4096.0, -20'000'000.0 + 4096.0, 19'996'672.0}) {
        for (const double z0 : {-5'830'000.0, -5'500'000.0, 5'830'000.0, 3'000'000.0}) {
            const double xb = std::floor(x0 / 8192.0) * 8192.0;
            const std::array<double, 2> w = real().ergs.windAt(xb, z0);
            const double ux = w[0] >= 0.0 ? w[0] : -w[0];
            const double uz = w[0] >= 0.0 ? w[1] : -w[1];
            const auto mean = [&](double from) {
                const std::vector<double> g = dunes_test::spacings(dunes_test::crestsAlong(
                    real().ergs, from, z0 - (xb - from) * uz / ux, ux, uz, 8'000.0, 4.0,
                    kTransverseSand, kSteady, 20.0));
                double m = 0.0;
                for (const double v : g) {
                    m += v / static_cast<double>(g.size());
                }
                return m;
            };
            const double before = mean(xb - 8'000.0 * ux);
            const double after = mean(xb);
            worst = std::max(worst, std::max(before, after) / std::min(before, after));
        }
    }
    std::printf("far boundaries: worst ratio of the spacings on each side %.2f\n", worst);
    TEST_CHECK(worst <= 1.5);
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
    testTransverse();
    testBarchans();
    testSeifs();
    testStars();
    testFarBoundary();
    testContinuous();
    return test::result();
}

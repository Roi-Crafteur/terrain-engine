// The waves of the ergs of the characters big waves and giant waves (agent 34, dune_waves.hpp):
// - the profile across a crest: the face at 33 degrees at the brink, the back gentle, no floor
//   between two waves; along the wind the backs at most 18 (big waves) or 14 degrees (giants);
// - on the default world (forced characters, sites far from the origin too): the spacing of the
//   crests (2 to 6 km, 6 to 20 km), their heights (at most 300 m, 300 to 800 m), almost no flat
//   ground, no step anywhere; across the boundary of a cell of the pivots (16 and 65 km), far
//   from the origin, the spacing holds;
// - the far horizon: the same bits as the tiles below a tenth of their spacing (no seam), their
//   mean beyond without flicker.
#include "dune_waves.hpp"
#include "dunes_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using dunes_test::real;
using terrain::ErgCharacter;

constexpr double kTan33 = 0.6494075931975106;

void testProfile() {
    for (const double backTan : {0.21255656167002213, 0.10510423526567646}) {
        const double wavelength = 10'000.0;
        const double height = 300.0;
        const auto h = [&](double t) {
            return terrain::dune_waves::profile(t, wavelength, height, backTan);
        };
        const double dt = 0.01 / wavelength;
        const double brink = (h(0.0) - h(dt)) / 0.01;
        double steepestBack = 0.0;
        double low = 0.0;
        for (double t = 0.3; t < 1.0; t += 1e-4) {
            const double s = (h(t + dt) - h(t)) / 0.01;
            if (s > 0.0) {
                steepestBack = std::max(steepestBack, s);
            }
        }
        for (double t = 0.0; t < 1.0; t += 1e-4) {
            low += h(t) < 0.01 * height ? 1e-4 : 0.0;
        }
        std::printf("profile (back at most %.1f deg): brink %.2f deg, steepest back %.2f deg, "
                    "under 1 %% of the height %.2f %% of the wavelength\n",
                    std::atan(backTan) * dunes_test::kDegrees, std::atan(brink) * dunes_test::kDegrees,
                    std::atan(steepestBack) * dunes_test::kDegrees, 100.0 * low);
        TEST_CHECK(std::abs(brink - kTan33) < 0.01);
        TEST_CHECK(steepestBack <= backTan * 1.01);
        TEST_CHECK(low < 0.05); // the trough only
        TEST_CHECK(h(0.0) == height);
    }
}

struct Site {
    double x;
    double z;
};
const std::vector<Site> kSites = {
    {0.0, 3'000'000.0},           {12'345'678.0, -4'000'000.0}, {-20'000'000.0, 2'600'000.0},
    {-7'654'321.0, 5'000'000.0},  {19'999'744.0, 5'830'000.0},
};

// Whether h[i] is a crest: over floor and the highest within reach samples on each side (the
// dunes laid on the backs of the waves are no crests of the waves).
bool crestAt(const std::vector<double>& h, std::size_t i, double floor, std::size_t reach) {
    if (!(h[i] > floor) || i < reach || i + reach >= h.size()) {
        return false;
    }
    for (std::size_t k = i - reach; k <= i + reach; ++k) {
        if (h[k] > h[i] || (k < i && h[k] == h[i])) {
            return false;
        }
    }
    return true;
}

// The forms of a character along the wind at the sites: spacing, heights, slopes, flat ground.
void testForms(ErgCharacter c, double spacingLow, double spacingHigh, double tallest,
               double backMost) {
    const bool giant = c == ErgCharacter::Giants;
    const double length = giant ? 120'000.0 : 40'000.0;
    const double step = giant ? 20.0 : 8.0;
    std::vector<double> gaps;
    std::vector<double> heights;
    std::vector<double> slopes;
    double steepest = 0.0;
    for (const Site& s : kSites) {
        const std::array<double, 2> w = real().ergs.windAt(s.x, s.z);
        std::vector<double> h;
        for (double d = 0.0; d <= length; d += step) {
            h.push_back(dunes_test::forcedDunes(real().ergs, s.x + d * w[0], s.z + d * w[1], 0.6f,
                                                0.1f, c));
        }
        double last = -1.0;
        for (std::size_t i = 1; i + 1 < h.size(); ++i) {
            heights.push_back(h[i]);
            slopes.push_back(std::atan(std::abs(h[i + 1] - h[i]) / step) * dunes_test::kDegrees);
            if (crestAt(h, i, 0.3 * tallest, static_cast<std::size_t>(0.25 * spacingLow / step))) {
                if (last >= 0.0) {
                    gaps.push_back(static_cast<double>(i) * step - last);
                }
                last = static_cast<double>(i) * step;
            }
        }
        for (int k = 0; k < 2000; ++k) {
            const double x = s.x + 7.31 * k;
            const double z = s.z + 3.17 * k;
            const double a = dunes_test::forcedDunes(real().ergs, x, z, 0.6f, 0.1f, c);
            const double b = dunes_test::forcedDunes(real().ergs, x + 0.25, z, 0.6f, 0.1f, c);
            steepest = std::max(steepest, std::abs(b - a) / 0.25);
        }
    }
    const double top = *std::max_element(heights.begin(), heights.end());
    // Flat ground: under an eighth of the steepest back along the wind (1.5 degrees for the big
    // waves, 0.8 for the giant waves, whose backs are 3 degrees on average): the troughs.
    std::size_t flat = 0;
    for (const double v : slopes) {
        flat += v < backMost / 8.0 ? 1 : 0;
    }
    const double flatShare = static_cast<double>(flat) / static_cast<double>(slopes.size());
    std::printf("%s: %zu spacings, median %.0f m (p10 %.0f, p90 %.0f); heights p50 %.0f, highest "
                "%.0f m; slopes along the wind p80 %.1f p99 %.1f deg; steepest step %.1f deg; flat "
                "%.1f %%\n",
                giant ? "giant waves" : "big waves", gaps.size(), dunes_test::quantile(gaps, 0.5),
                dunes_test::quantile(gaps, 0.1), dunes_test::quantile(gaps, 0.9),
                dunes_test::quantile(heights, 0.5), top, dunes_test::quantile(slopes, 0.8),
                dunes_test::quantile(slopes, 0.99), std::atan(steepest) * dunes_test::kDegrees,
                100.0 * flatShare);
    TEST_CHECK(gaps.size() >= 10);
    const double median = dunes_test::quantile(gaps, 0.5);
    TEST_CHECK(median >= spacingLow && median <= spacingHigh);
    TEST_CHECK(top <= tallest + 1e-3 && top >= 0.5 * tallest);
    TEST_CHECK(dunes_test::quantile(slopes, 0.8) <= backMost); // most of the ground: the backs
    TEST_CHECK(std::atan(steepest) * dunes_test::kDegrees <= 40.0);
    TEST_CHECK(flatShare <= 0.1);
}


// The mean spacing of the crests of a character along (ux, uz) from (x, z) over length meters.
double meanSpacing(ErgCharacter c, double x, double z, double ux, double uz, double length,
                   double step, double floor) {
    std::vector<double> h;
    for (double d = 0.0; d <= length; d += step) {
        h.push_back(dunes_test::forcedDunes(real().ergs, x + d * ux, z + d * uz, 0.6f, 0.1f, c));
    }
    std::vector<double> crests;
    const double reach = c == ErgCharacter::Giants ? 1'500.0 : 500.0;
    for (std::size_t i = 1; i + 1 < h.size(); ++i) {
        if (crestAt(h, i, floor, static_cast<std::size_t>(reach / step))) {
            crests.push_back(static_cast<double>(i) * step);
        }
    }
    return crests.size() < 2 ? 0.0 : (crests.back() - crests.front()) / static_cast<double>(crests.size() - 1);
}

// Far from the origin, across the boundary of a cell of the planet level where the wind turns,
// the spacing of the crests holds within 1.5 times on each side (over 5 crests or so each).
void testFarBoundary(ErgCharacter c, double length, double step, double floor) {
    double worst = 1.0;
    for (const double x0 : {20'000'000.0 - 4096.0, -20'000'000.0 + 4096.0}) {
        for (const double z0 : {-5'830'000.0, 5'830'000.0, 3'000'000.0}) {
            const double xb = std::floor(x0 / 8192.0) * 8192.0;
            const std::array<double, 2> w = real().ergs.windAt(xb, z0);
            const double ux = w[0] >= 0.0 ? w[0] : -w[0];
            const double uz = w[0] >= 0.0 ? w[1] : -w[1];
            const double before = meanSpacing(c, xb - length * ux, z0 - length * uz, ux, uz,
                                              length, step, floor);
            const double after = meanSpacing(c, xb, z0, ux, uz, length, step, floor);
            if (before > 0.0 && after > 0.0) {
                worst = std::max(worst, std::max(before, after) / std::min(before, after));
            }
        }
    }
    std::printf("%s far boundaries: worst ratio of the spacings on each side %.2f\n",
                c == ErgCharacter::Giants ? "giant waves" : "big waves", worst);
    TEST_CHECK(worst <= 1.5);
}

// A point of a full erg of giant waves of the default world.
bool giantSite(double& x0, double& z0) {
    for (const terrain::ErgSummary& erg : real().ergs.ergs()) {
        if (erg.character != ErgCharacter::Giants) {
            continue;
        }
        for (int j = -10; j <= 10; ++j) {
            for (int i = -10; i <= 10; ++i) {
                const double x = erg.x + 65'536.0 * i;
                const double z = erg.z + 65'536.0 * j;
                if (!real().ergs.mayHold(x, z)) {
                    continue;
                }
                const terrain::ErgPoint e = real().ergs.at(x, z, 0.0f);
                if (e.giants > 0.99f && e.mask > 0.9f) {
                    x0 = x;
                    z0 = z;
                    return true;
                }
            }
        }
    }
    return false;
}

// The far horizon over the giant waves: the same bits as a point (the tiles) up to a pitch of
// 512 m (no seam where the tiles meet it, the crests whole on its near levels); beyond, their
// mean, which a sample half a pitch away barely changes (no flicker).
void testHorizon() {
    double x0 = 0.0;
    double z0 = 0.0;
    TEST_CHECK(giantSite(x0, z0));
    terrain::HeightSample s;
    std::size_t same = 0;
    std::size_t count = 0;
    double flicker = 0.0;
    double range = 0.0;
    for (int k = 0; k < 400; ++k) {
        const double x = x0 + 97.3 * k;
        const double z = z0 + 41.9 * k;
        const float point = real().ergs.dunesAt(x, z, s);
        for (const double pitch : {8.0, 64.0, 256.0, 512.0}) {
            ++count;
            same += real().ergs.dunesAt(x, z, s, pitch) == point ? 1 : 0;
        }
        const double pitch = 4096.0;
        const double a = static_cast<double>(real().ergs.dunesAt(x, z, s, pitch));
        const double b = static_cast<double>(real().ergs.dunesAt(x + 0.5 * pitch, z, s, pitch));
        flicker = std::max(flicker, std::abs(b - a));
        range = std::max(range, static_cast<double>(point));
    }
    std::printf("horizon over the giant waves: %zu of %zu samples up to 512 m the same bits as "
                "the points; at 4096 m, half a pitch moves them by %.1f m at most (crests up to "
                "%.0f m)\n",
                same, count, flicker, range);
    TEST_CHECK(same == count);
    TEST_CHECK(flicker <= 0.1 * range);
}

// A point of a full erg of a character of the default world.
bool siteOf(ErgCharacter c, double& x0, double& z0) {
    for (const terrain::ErgSummary& erg : real().ergs.ergs()) {
        if (erg.character != c) {
            continue;
        }
        for (int j = -10; j <= 10; ++j) {
            for (int i = -10; i <= 10; ++i) {
                const double x = erg.x + 65'536.0 * i;
                const double z = erg.z + 65'536.0 * j;
                if (!real().ergs.mayHold(x, z)) {
                    continue;
                }
                const terrain::ErgPoint e = real().ergs.at(x, z, 0.0f);
                const float weight = c == ErgCharacter::Giants
                                         ? e.giants
                                         : (c == ErgCharacter::Waves ? e.waves
                                                                     : 1.0f - e.waves - e.giants);
                if (weight > 0.99f && e.mask > 0.9f) {
                    x0 = x;
                    z0 = z;
                    return true;
                }
            }
        }
    }
    return false;
}

// The ergs bury the local relief of the geography (base over regional): over a butte or a hollow
// of 30 m the surface of the sand is the same, the giant waves and the realistic chains alike.
void testBuried() {
    double gx = 0.0;
    double gz = 0.0;
    double rx = 0.0;
    double rz = 0.0;
    TEST_CHECK(siteOf(ErgCharacter::Giants, gx, gz) && siteOf(ErgCharacter::Realistic, rx, rz));
    double worst = 0.0;
    bool realistic = true;
    for (int k = 0; k < 200; ++k) {
        terrain::HeightSample flat;
        const double x = gx + 151.3 * k;
        const double z = gz + 67.9 * k;
        const double surface = static_cast<double>(real().ergs.dunesAt(x, z, flat));
        for (const float local : {-30.0f, 30.0f}) {
            terrain::HeightSample s;
            s.base = local;
            const double sand = static_cast<double>(real().ergs.dunesAt(x, z, s));
            if (surface >= 30.0) {
                worst = std::max(worst, std::abs(local + sand - surface));
            }
            const double rx1 = rx + 151.3 * k;
            const double rz1 = rz + 67.9 * k;
            const double chains = static_cast<double>(real().ergs.dunesAt(rx1, rz1, flat));
            if (chains >= 30.0) {
                const double over = static_cast<double>(real().ergs.dunesAt(rx1, rz1, s));
                realistic = realistic && std::abs(local + over - chains) < 1e-3;
            }
        }
    }
    std::printf("buried: over a butte or a hollow of 30 m the sand of the giant waves moves its "
                "surface by %.4f m at most; the realistic chains %s\n",
                worst, realistic ? "too" : "NOT");
    TEST_CHECK(worst < 1e-3);
    TEST_CHECK(realistic);
}
} // namespace

int main() {
    testProfile();
    // The backs: at most 18 degrees for the big waves, 14 for the giants (validated at the third
    // stop of agent 34).
    testForms(ErgCharacter::Waves, 2'000.0, 6'000.0, 300.0, 18.0);
    testForms(ErgCharacter::Giants, 6'000.0, 20'000.0, 800.0, 14.0);
    testFarBoundary(ErgCharacter::Waves, 16'000.0, 8.0, 40.0);
    testFarBoundary(ErgCharacter::Giants, 100'000.0, 25.0, 150.0);
    testHorizon();
    testBuried();
    return test::result();
}

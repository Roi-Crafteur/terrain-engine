// The waves of the ergs of the characters big waves and giant waves (agent 34, dune_waves.hpp):
// - the profile across a crest: the face at 33 degrees past its rounded brink, the back gentle,
//   no floor between two waves; along the wind the backs at most 18 (big waves) or 16 degrees
//   (giants);
// - on the default world (forced characters, sites far from the origin too): the spacing of the
//   crests (1.2 to 6 km, 5 to 20 km), their heights (at most 300 m, 300 to 760 m), almost no flat
//   ground, no step anywhere, nor across the boundaries of the cells of the planet level far from
//   the origin;
// - the far horizon: the same bits as the tiles below a tenth of their spacing (no seam), their
//   mean beyond without flicker.
#include "dune_blend.hpp"
#include "dune_forms.hpp"
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
        // The brink: just past the rounding of the crest (dune_forms::kCrestRound).
        const double past = (2.0 * terrain::dune_forms::kCrestRound / kTan33 + 0.01) / wavelength;
        const double brink = (h(past) - h(past + dt)) / 0.01;
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
        TEST_CHECK(std::abs(h(0.0) - (height - terrain::dune_forms::kCrestRound)) < 1e-9);
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
// The backs (backMost, 80th percentile of the slopes along the wind) on the profile smoothed over
// 300 m: the slope of the wave, not of the crescents laid on it (their slip faces cover the backs
// of the giant waves since the eighth stop; on the raw profile, 17.4 degrees), the raw profile
// under 20 degrees still (the measure smoothed may not drift).
void testForms(ErgCharacter c, double spacingLow, double spacingHigh, double tallest,
               double backMost) {
    const bool giant = c == ErgCharacter::Giants;
    const double length = giant ? 120'000.0 : 40'000.0;
    const double step = giant ? 20.0 : 8.0;
    std::vector<double> gaps;
    std::vector<double> heights;
    std::vector<double> slopes;
    std::vector<double> backs;
    double steepest = 0.0;
    for (const Site& s : kSites) {
        const std::array<double, 2> w = real().ergs.windAt(s.x, s.z);
        std::vector<double> h;
        for (double d = 0.0; d <= length; d += step) {
            h.push_back(dunes_test::forcedDunes(real().ergs, s.x + d * w[0], s.z + d * w[1], 0.6f,
                                                0.1f, c));
        }
        // The profile smoothed over 300 m (a running mean).
        const auto half = static_cast<std::size_t>(150.0 / step);
        std::vector<double> smooth(h.size(), 0.0);
        for (std::size_t i = half; i + half < h.size(); ++i) {
            for (std::size_t j = i - half; j <= i + half; ++j) {
                smooth[i] += h[j] / static_cast<double>(2 * half + 1);
            }
        }
        for (std::size_t i = half; i + half + 1 < h.size(); ++i) {
            backs.push_back(std::atan(std::abs(smooth[i + 1] - smooth[i]) / step) *
                            dunes_test::kDegrees);
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
                "%.0f m; slopes along the wind p80 %.1f p99 %.1f deg (smoothed over 300 m p80 %.1f); "
                "steepest step %.1f deg; flat %.1f %%\n",
                giant ? "giant waves" : "big waves", gaps.size(), dunes_test::quantile(gaps, 0.5),
                dunes_test::quantile(gaps, 0.1), dunes_test::quantile(gaps, 0.9),
                dunes_test::quantile(heights, 0.5), top, dunes_test::quantile(slopes, 0.8),
                dunes_test::quantile(slopes, 0.99), dunes_test::quantile(backs, 0.8),
                std::atan(steepest) * dunes_test::kDegrees,
                100.0 * flatShare);
    TEST_CHECK(gaps.size() >= 10);
    const double median = dunes_test::quantile(gaps, 0.5);
    TEST_CHECK(median >= spacingLow && median <= spacingHigh);
    TEST_CHECK(top <= tallest + 1e-3 && top >= 0.5 * tallest);
    TEST_CHECK(dunes_test::quantile(backs, 0.8) <= backMost); // most of the ground: the backs
    TEST_CHECK(dunes_test::quantile(slopes, 0.8) <= 20.0);    // and with the dunes laid on them
    TEST_CHECK(std::atan(steepest) * dunes_test::kDegrees <= 40.0);
    TEST_CHECK(flatShare <= 0.1);
}


// Far from the origin, across the boundary of a cell of the planet level where the wind turns:
// no step, no seam (the spacings of two windows side by side differ by up to 3 times even far
// from any boundary, with the ends and the joins of the crests: they tell nothing of it).
void testFarBoundary(ErgCharacter c) {
    double steepest = 0.0;
    for (const double x0 : {20'000'000.0 - 4096.0, -20'000'000.0 + 4096.0}) {
        for (const double z0 : {-5'830'000.0, 5'830'000.0, 3'000'000.0}) {
            const double xb = std::floor(x0 / 8192.0) * 8192.0;
            double last = dunes_test::forcedDunes(real().ergs, xb - 50.0, z0, 0.6f, 0.1f, c);
            for (double d = -49.75; d <= 50.0; d += 0.25) {
                const double h = dunes_test::forcedDunes(real().ergs, xb + d, z0, 0.6f, 0.1f, c);
                steepest = std::max(steepest, std::abs(h - last) / 0.25);
                last = h;
            }
        }
    }
    std::printf("%s far boundaries: steepest step across them %.1f deg\n",
                c == ErgCharacter::Giants ? "giant waves" : "big waves",
                std::atan(steepest) * dunes_test::kDegrees);
    TEST_CHECK(std::atan(steepest) * dunes_test::kDegrees <= 40.0);
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
// The joins of the second system (the straight pale ribbons seen from 1 km at the sixth stop):
// no fold where they meet the first one (the angle between the slopes either side of the meeting
// under half a degree, where a plain maximum made 45 on that line), and no lift over the backs
// and troughs of the second system, only near its crests. Nor a jump of its curvature (the fine
// thread left at the seventh stop, the curvature of a quadratic join jumping by 1/(2 soft) at its
// ends): the second differences either side of any point of the meeting, 0.5 m apart, differ by
// under a tenth of that jump.
void testJoins() {
    using terrain::dune_waves::joined;
    const double soft = 45.0;
    double worst = 0.0;
    double jump = 0.0; // of the curvature, times soft
    for (const double main : {-40.0, 0.0, 120.0, 250.0}) {
        for (const double top : {1.0, 0.7}) {
            // The join crossing the first system along a line, 1 m of it per meter of ground.
            const auto h = [&](double d) { return joined(main, main + d, top, 1.0, soft); };
            for (double d = -60.0; d <= 60.0; d += 0.5) {
                const double before = h(d) - h(d - 0.5);
                const double after = h(d + 0.5) - h(d);
                worst = std::max(worst, std::abs(std::atan(2.0 * after) - std::atan(2.0 * before)) *
                                            dunes_test::kDegrees);
                const double curveBefore = h(d) - 2.0 * h(d - 0.5) + h(d - 1.0);
                const double curveAfter = h(d + 1.0) - 2.0 * h(d + 0.5) + h(d);
                jump = std::max(jump, std::abs(curveAfter - curveBefore) / 0.25 * soft);
            }
        }
    }
    std::printf("joins: worst fold %.2f deg, curvature jump %.4f / soft\n", worst, jump);
    TEST_CHECK(worst <= 0.5);
    TEST_CHECK(jump <= 0.05);
    // Over the back or the trough of the second system: nothing; over its crest: all of it.
    TEST_CHECK(joined(0.0, 200.0, 0.05, 1.0, soft) == 0.0);
    TEST_CHECK(joined(-50.0, 180.0, -0.3, 1.0, soft) == -50.0);
    TEST_CHECK(std::abs(joined(0.0, 200.0, 1.0, 1.0, soft) - 200.0) < 1e-9);
    TEST_CHECK(std::abs(joined(0.0, 200.0, 1.0, 0.5, soft) - 100.0) < 1e-9);
    TEST_CHECK(joined(150.0, 100.0, 1.0, 1.0, soft) == 150.0);
}

// The rock out of the sand (the grey patches with hard edges of the sixth stop): the share of the
// surface the sand covers falls from 1 to 0 smoothly (at most 0.4 per meter of ground, along the
// slip faces too, where a flag made it jump from 1 to 0), past 2 to 28 m of rock over the sand, how many wandering from
// one place to the next (the sand climbs the foot irregularly: 8 m between the 10th and the 90th
// percentiles at least); 1 everywhere off the rock (the rock deep under the troughs of the giant
// waves stands out of them too).
void testRockFoot() {
    double gx = 0.0;
    double gz = 0.0;
    TEST_CHECK(siteOf(ErgCharacter::Giants, gx, gz));
    double steepest = 0.0;
    std::vector<double> climbs;
    bool off = true;
    for (int line = 0; line < 32; ++line) {
        const double z = gz + 397.0 * line;
        double last = -1.0;
        for (double d = 0.0; d <= 1300.0; d += 0.5) {
            const double x = gx + 1733.0 * line + d;
            terrain::HeightSample s;
            s.base = static_cast<float>(0.2 * d); // a slope of 11 degrees out of the sand
            float sand = 0.0f;
            (void)real().ergs.dunesAt(x, z, s, 0.0, nullptr, &sand);
            const double share = static_cast<double>(sand);
            // The rock: kBuryMost under the local relief, kRockFall times as deep under 60 m.
            const double local = 0.2 * d;
            const double rock = (local - 60.0) * (local > 60.0 ? 1.0 : 5.0);
            terrain::HeightSample flat;
            const double out = rock - static_cast<double>(real().ergs.dunesAt(x, z, flat));
            if (last >= 0.0) {
                steepest = std::max(steepest, std::abs(share - last) / 0.5);
            }
            if (last >= 0.5 && share < 0.5) {
                climbs.push_back(out);
            }
            off = off && (out > 2.0 || share == 1.0);
            last = share;
        }
    }
    const double p10 = dunes_test::quantile(climbs, 0.1);
    const double p90 = dunes_test::quantile(climbs, 0.9);
    std::printf("rock foot: %zu edges, the sand gives way to the rock %.1f to %.1f m over it (p10, "
                "p90), at most %.3f of its share per meter\n",
                climbs.size(), p10, p90, steepest);
    TEST_CHECK(climbs.size() >= 20);
    TEST_CHECK(steepest <= 0.4);
    TEST_CHECK(p10 >= 2.0 && p90 <= 28.0 && p90 - p10 >= 8.0);
    TEST_CHECK(off);
}

// The avalanche face of the giant waves (the eighth stop: their wall of 700 m read as a low smooth
// hill): straight at the angle of repose down 35 % of their height, then the parabola, its slope
// continuous where they meet; without it (slip 0) the parabola from the brink, as before.
void testSlipFace() {
    const double height = 700.0;
    const double wavelength = 12'000.0;
    const double backTan = 0.2867453857588079; // 16 degrees
    const auto slopeAt = [&](double d, double slip) {
        const auto h = [&](double v) {
            return terrain::dune_waves::shapeOf(v / wavelength, wavelength, height, backTan, 0.0,
                                                slip).height;
        };
        return std::atan((h(d) - h(d + 0.5)) / 0.5) * dunes_test::kDegrees;
    };
    const double run = 0.35 * height / kTan33;
    double straightLeast = 90.0;
    double straightMost = 0.0;
    for (double d = 0.1 * run; d <= 0.95 * run; d += 0.05 * run) {
        straightLeast = std::min(straightLeast, slopeAt(d, 0.35));
        straightMost = std::max(straightMost, slopeAt(d, 0.35));
    }
    const double join = std::abs(slopeAt(run + 0.5, 0.35) - slopeAt(run - 1.0, 0.35));
    std::printf("slip face: %.1f to %.1f deg down %.0f m, %.2f deg change where it meets the curve; "
                "without it %.1f deg halfway\n",
                straightLeast, straightMost, 0.35 * height, join, slopeAt(0.5 * run, 0.0));
    TEST_CHECK(straightLeast >= 32.5 && straightMost <= 33.5);
    TEST_CHECK(join <= 0.5);
    TEST_CHECK(slopeAt(0.5 * run, 0.0) < 31.0);
}

// A kind of a tiny weight in the blend (giants of 208 m at 0.3 % over the big waves, the thread
// of the seventh stop): as the big waves fall through the floor the surface follows them at their
// own slope, never held at a terrace of w times the height of the other kind (0.6 m, flat while
// the big waves stood under it).
void testMinorKind() {
    double least = 1.0;
    for (double h = -3.0; h <= 3.0; h += 0.05) {
        const auto at = [](double v) {
            terrain::dune_field::Blend b;
            b.add(v, 0.997f);
            b.add(208.0, 0.003f);
            return b.value();
        };
        least = std::min(least, (at(h + 0.05) - at(h)) / 0.05);
    }
    std::printf("minor kind: the slope of the blend over the floor %.3f of the main kind's at least\n",
                least / 0.997);
    TEST_CHECK(least >= 0.9 * 0.997);
}

} // namespace

int main() {
    testProfile();
    testJoins();
    testMinorKind();
    testSlipFace();
    // The backs: at most 18 degrees for the big waves, 16 for the giants (validated at the fourth
    // stop of agent 34).
    testForms(ErgCharacter::Waves, 1'200.0, 6'000.0, 300.0, 18.0);
    testForms(ErgCharacter::Giants, 5'000.0, 20'000.0, 760.0, 16.0);
    testFarBoundary(ErgCharacter::Waves);
    testFarBoundary(ErgCharacter::Giants);
    testHorizon();
    testBuried();
    testRockFoot();
    return test::result();
}

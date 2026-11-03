// The slip faces of the giant waves as the far horizon draws them, and the steepest sand of the
// ergs of giant waves (agent 34, ninth stop).
// - Light: on the face of a giant wave, its ground read every 8 m as the second level of the far
//   horizon reads it (a footprint of 32 m: its detail normals), lit by a sun 20 degrees up, the
//   light less its mean over 80 m: along any direction its 2D autocorrelation over the face never
//   rises again by more than 0.3 past its lowest at a shorter lag, from 24 m on (a grid of chutes
//   40 to 80 m apart drew rows of spots there, a checkerboard: dc27b0f).
// - Sand: along lines every meter across 3 ergs of giant waves, no ground in the color of the sand
//   (the share of the erg and of the sand 0.99 at least) steeper than 40 degrees (the rock buried
//   under the troughs of the giant waves showed at 63 degrees in the color of the sand: dc27b0f).
#include "dunes_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using dunes_test::real;
using terrain::ErgCharacter;

constexpr double kTan40 = 0.8390996311772800;

// Sites of distinct ergs of giant waves (their giants and mask full), at most count.
std::vector<std::array<double, 2>> giantSites(std::size_t count) {
    std::vector<std::array<double, 2>> out;
    for (const terrain::ErgSummary& erg : real().ergs.ergs()) {
        if (erg.character != ErgCharacter::Giants || out.size() >= count) {
            continue;
        }
        bool found = false;
        for (int j = -10; j <= 10 && !found; ++j) {
            for (int i = -10; i <= 10 && !found; ++i) {
                const double x = erg.x + 65'536.0 * i;
                const double z = erg.z + 65'536.0 * j;
                if (!real().ergs.mayHold(x, z)) {
                    continue;
                }
                const terrain::ErgPoint e = real().ergs.at(x, z, 0.0f);
                if (e.giants > 0.99f && e.mask > 0.99f) {
                    out.push_back({x, z});
                    found = true;
                }
            }
        }
    }
    return out;
}

// The dunes of the giant waves at (x, z) over a flat relief, read at a footprint.
double dunesAt(double x, double z, double footprint) {
    const terrain::HeightSample flat;
    return static_cast<double>(real().ergs.dunesAt(x, z, flat, footprint));
}

// A point of the straight slip face of a giant wave near the site: a slope of 28 to 32 degrees
// down the steepest way, as steep 60 m above and below it (the straight face, 250 m long at least).
bool facePoint(const std::array<double, 2>& site, double& fx, double& fz) {
    const auto slope = [](double x, double z, double& gx, double& gz) {
        gx = (dunesAt(x + 1.0, z, 0.0) - dunesAt(x - 1.0, z, 0.0)) / 2.0;
        gz = (dunesAt(x, z + 1.0, 0.0) - dunesAt(x, z - 1.0, 0.0)) / 2.0;
        return std::hypot(gx, gz);
    };
    for (int j = -50; j <= 50; ++j) {
        for (int i = -50; i <= 50; ++i) {
            const double x = site[0] + 200.0 * i;
            const double z = site[1] + 200.0 * j;
            double gx = 0.0;
            double gz = 0.0;
            const double g = slope(x, z, gx, gz);
            if (g < 0.532 || g > 0.625) {
                continue;
            }
            bool straight = true;
            for (const double d : {-60.0, 60.0}) {
                double hx = 0.0;
                double hz = 0.0;
                straight = straight && slope(x + d * gx / g, z + d * gz / g, hx, hz) > 0.51;
            }
            if (straight) {
                fx = x;
                fz = z;
                return true;
            }
        }
    }
    return false;
}

// The largest autocorrelation of the light of the face, past a lag of 24 m (3 samples).
double faceRepeat(double fx, double fz) {
    constexpr int kN = 128;
    constexpr double kStep = 8.0;
    std::vector<double> h(static_cast<std::size_t>(kN * kN));
    for (int k = 0; k < kN; ++k) {
        for (int i = 0; i < kN; ++i) {
            h[static_cast<std::size_t>(k * kN + i)] =
                dunesAt(fx + (i - kN / 2) * kStep, fz + (k - kN / 2) * kStep, 32.0);
        }
    }
    const auto at = [&](int i, int k) { return h[static_cast<std::size_t>(k * kN + i)]; };
    const double sun[3] = {std::sin(3.4906585) * std::cos(0.3490659), std::sin(0.3490659),
                           -std::cos(3.4906585) * std::cos(0.3490659)};
    std::vector<double> light(h.size(), 0.0);
    std::vector<char> face(h.size(), 0);
    for (int k = 1; k + 1 < kN; ++k) {
        for (int i = 1; i + 1 < kN; ++i) {
            const double gx = (at(i + 1, k) - at(i - 1, k)) / (2.0 * kStep);
            const double gz = (at(i, k + 1) - at(i, k - 1)) / (2.0 * kStep);
            const double n = std::sqrt(gx * gx + gz * gz + 1.0);
            light[static_cast<std::size_t>(k * kN + i)] = (-gx * sun[0] + sun[1] - gz * sun[2]) / n;
            face[static_cast<std::size_t>(k * kN + i)] = std::hypot(gx, gz) > 0.364 ? 1 : 0;
        }
    }
    // Less its mean over 80 m.
    std::vector<double> detail(h.size(), 0.0);
    for (int k = 10; k + 10 < kN; ++k) {
        for (int i = 10; i + 10 < kN; ++i) {
            double mean = 0.0;
            for (int dk = -5; dk <= 5; ++dk) {
                for (int di = -5; di <= 5; ++di) {
                    mean += light[static_cast<std::size_t>((k + dk) * kN + i + di)] / 121.0;
                }
            }
            detail[static_cast<std::size_t>(k * kN + i)] =
                light[static_cast<std::size_t>(k * kN + i)] - mean;
        }
    }
    const auto r = [&](int lx, int lz) {
        double sum = 0.0;
        double na = 0.0;
        double nb = 0.0;
        for (int k = 24; k + 24 < kN; ++k) {
            for (int i = 24; i + 24 < kN; ++i) {
                const auto a = static_cast<std::size_t>(k * kN + i);
                const auto b = static_cast<std::size_t>((k + lz) * kN + i + lx);
                if (face[a] != 0 && face[b] != 0) {
                    sum += detail[a] * detail[b];
                    na += detail[a] * detail[a];
                    nb += detail[b] * detail[b];
                }
            }
        }
        return na > 0.0 && nb > 0.0 ? sum / std::sqrt(na * nb) : 0.0;
    };
    std::size_t faces = 0;
    for (const char f : face) {
        faces += f != 0 ? 1 : 0;
    }
    std::printf("face: %zu of %zu samples steeper than 20 degrees\n", faces, face.size());
    // Along each of 16 directions, how far the correlation rises again past its lowest at a
    // shorter lag (a smooth light only decays; rows of spots rise again at their period).
    double most = 0.0;
    for (int a = 0; a < 16; ++a) {
        const double ux = std::cos(a * 0.19634954);
        const double uz = std::sin(a * 0.19634954);
        double lowest = 1.0;
        for (int n = 1; n <= 12; ++n) {
            const double v = r(static_cast<int>(std::lround(n * ux)), static_cast<int>(std::lround(n * uz)));
            if (n >= 3) {
                most = std::max(most, v - lowest);
            }
            lowest = std::min(lowest, v);
        }
    }
    double chutes = 0.0;
    for (int k = 0; k < kN; k += 4) {
        for (int i = 0; i < kN; i += 4) {
            const double x = fx + (i - kN / 2) * kStep;
            const double z = fz + (k - kN / 2) * kStep;
            chutes = std::max(chutes, std::abs(dunesAt(x, z, 0.0) - at(i, k)));
        }
    }
    std::printf("face: the chutes and all, %.2f m from the ground of the horizon at most\n", chutes);
    return most;
}

void testFaceLight() {
    const std::vector<std::array<double, 2>> sites = giantSites(1);
    TEST_CHECK(!sites.empty());
    double fx = 0.0;
    double fz = 0.0;
    TEST_CHECK(!sites.empty() && facePoint(sites[0], fx, fz));
    const double repeat = faceRepeat(fx, fz);
    std::printf("face of a giant wave at (%.0f, %.0f), as the second level of the horizon reads "
                "it: its light rises again by %.2f at most past 24 m\n",
                fx, fz, repeat);
    TEST_CHECK(repeat <= 0.3);
}

void testSteepSand() {
    const std::vector<std::array<double, 2>> sites = giantSites(3);
    TEST_CHECK(sites.size() == 3);
    double steepest = 0.0;
    std::size_t sand = 0;
    for (const std::array<double, 2>& site : sites) {
        const std::array<double, 2> w = real().ergs.windAt(site[0], site[1]);
        for (const std::array<double, 2> u : {w, {-w[1], w[0]}}) {
            double last = 0.0;
            bool lastSand = false;
            for (double d = -3'000.0; d <= 3'000.0; d += 1.0) {
                const double x = site[0] + d * u[0];
                const double z = site[1] + d * u[1];
                const terrain::HeightSample s = real().relief.sampleAt(x, z);
                float mask = 0.0f;
                float share = 0.0f;
                const double h = static_cast<double>(s.height) +
                                 static_cast<double>(real().ergs.dunesAt(x, z, s, 0.0, &mask, &share));
                const bool isSand = mask >= 0.99f && share >= 0.99f;
                if (isSand && lastSand) {
                    steepest = std::max(steepest, std::abs(h - last));
                }
                sand += isSand ? 1 : 0;
                last = h;
                lastSand = isSand;
            }
        }
    }
    // And over a buried hill rising at 22 degrees under them (its rock 5 times as steep under the
    // troughs: the 63 degrees of (-790 488, 4 376 888) on the planet of the game).
    double hill = 0.0;
    std::size_t hillSand = 0;
    for (int line = 0; line < 32; ++line) {
        double last = 0.0;
        bool lastSand = false;
        for (double d = 0.0; d <= 600.0; d += 0.5) {
            const double x = sites[0][0] + 1'733.0 * line + d;
            const double z = sites[0][1] + 397.0 * line;
            terrain::HeightSample s;
            s.base = static_cast<float>(std::clamp(0.4 * (d - 300.0), -60.0, 60.0));
            float mask = 0.0f;
            float share = 0.0f;
            const double h = static_cast<double>(s.base) +
                             static_cast<double>(real().ergs.dunesAt(x, z, s, 0.0, &mask, &share));
            const bool isSand = mask >= 0.99f && share >= 0.99f;
            if (isSand && lastSand) {
                hill = std::max(hill, std::abs(h - last) / 0.5);
            }
            hillSand += isSand ? 1 : 0;
            last = h;
            lastSand = isSand;
        }
    }
    std::printf("sand of %zu ergs of giant waves: %zu points, the steepest %.1f deg; over a buried "
                "hill, %zu points, the steepest %.1f deg\n",
                sites.size(), sand, std::atan(steepest) * dunes_test::kDegrees, hillSand,
                std::atan(hill) * dunes_test::kDegrees);
    TEST_CHECK(hillSand >= 10'000);
    TEST_CHECK(hill <= kTan40);
    TEST_CHECK(sand >= 20'000);
    TEST_CHECK(steepest <= kTan40);
}

} // namespace

int main() {
    testFaceLight();
    testSteepSand();
    return test::result();
}

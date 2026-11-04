// The slip faces of the giant waves as the far horizon draws them, and the steepest sand of the
// ergs of giant waves (agent 34, ninth stop).
// - Light: on the faces of giant waves of 3 ergs, their ground read every 8 m as the second level
//   of the far horizon reads it (a footprint of 32 m: its detail normals), and every 2, 4 and 8 m
//   as the tiles and its first level draw them at 1 km (footprints 0, 0, 8), lit by a sun 20
//   degrees up, the light less its mean over 80 m: along any direction its 2D autocorrelation over
//   1 km of the faces (their mean) never rises again by more than 0.3 over zero (or its lowest)
//   from 24 to 96 m (chutes 40 to 80 m apart drew a checkerboard at 2 km, dc27b0f: 1.59 by the
//   first form of this measure; a comb of them, starting and ending alike, rows of light blocks
//   at 1 km, f9c3391: 0.64).
// - Sand: along lines every meter across 3 ergs of giant waves, no ground in the color of the sand
//   (the share of the erg and of the sand 0.99 at least) steeper than 40 degrees (the rock buried
//   under the troughs of the giant waves showed at 63 degrees in the color of the sand: dc27b0f);
//   over a buried hill, no steep rock 6 m over the sand draped on it in the color of the sand.
// - Crescents: around the faces of 3 ergs of giant waves, in 2D, nothing steeper than 37 degrees
//   (tenth stop: the crescents on their backs read where they lie at the point, 46 degrees).
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
constexpr double kTan30 = 0.5773502691896257;
constexpr double kTan37 = 0.7535540501027942;

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

// The light of the face of a giant wave around (fx, fz), its ground read every step meters at a
// footprint, lit by a sun 20 degrees up from the south-west, less its mean over 80 m; face marks
// the samples steeper than 20 degrees.
struct FaceLight {
    int n = 0;
    std::vector<double> detail;
    std::vector<char> face;
};

FaceLight faceLight(double fx, double fz, double step, double footprint, double side) {
    FaceLight f;
    f.n = static_cast<int>(side / step);
    const int n = f.n;
    const auto idx = [n](int i, int k) { return static_cast<std::size_t>(k * n + i); };
    std::vector<double> h(static_cast<std::size_t>(n * n));
    for (int k = 0; k < n; ++k) {
        for (int i = 0; i < n; ++i) {
            h[idx(i, k)] = dunesAt(fx + (i - n / 2) * step, fz + (k - n / 2) * step, footprint);
        }
    }
    const double sun[3] = {std::sin(3.4906585) * std::cos(0.3490659), std::sin(0.3490659),
                           -std::cos(3.4906585) * std::cos(0.3490659)};
    std::vector<double> light(h.size(), 0.0);
    f.face.assign(h.size(), 0);
    for (int k = 1; k + 1 < n; ++k) {
        for (int i = 1; i + 1 < n; ++i) {
            const double gx = (h[idx(i + 1, k)] - h[idx(i - 1, k)]) / (2.0 * step);
            const double gz = (h[idx(i, k + 1)] - h[idx(i, k - 1)]) / (2.0 * step);
            light[idx(i, k)] =
                (-gx * sun[0] + sun[1] - gz * sun[2]) / std::sqrt(gx * gx + gz * gz + 1.0);
            f.face[idx(i, k)] = std::hypot(gx, gz) > 0.364 ? 1 : 0;
        }
    }
    // Less its mean over 80 m (a summed table).
    std::vector<double> sum(static_cast<std::size_t>((n + 1) * (n + 1)), 0.0);
    for (int k = 0; k < n; ++k) {
        for (int i = 0; i < n; ++i) {
            sum[static_cast<std::size_t>((k + 1) * (n + 1) + i + 1)] =
                light[idx(i, k)] + sum[static_cast<std::size_t>(k * (n + 1) + i + 1)] +
                sum[static_cast<std::size_t>((k + 1) * (n + 1) + i)] -
                sum[static_cast<std::size_t>(k * (n + 1) + i)];
        }
    }
    const int r = static_cast<int>(std::lround(40.0 / step));
    const auto s = [&](int i, int k) { return sum[static_cast<std::size_t>(k * (n + 1) + i)]; };
    f.detail.assign(h.size(), 0.0);
    for (int k = r + 1; k + r + 1 < n; ++k) {
        for (int i = r + 1; i + r + 1 < n; ++i) {
            const double box = s(i + r + 1, k + r + 1) - s(i - r, k + r + 1) - s(i + r + 1, k - r) +
                               s(i - r, k - r);
            f.detail[idx(i, k)] = light[idx(i, k)] - box / ((2 * r + 1) * (2 * r + 1));
        }
    }
    return f;
}

// The correlation of the light of the face with itself lx, lz samples away.
double correlation(const FaceLight& f, int lx, int lz, int margin) {
    const int n = f.n;
    double sum = 0.0;
    double na = 0.0;
    double nb = 0.0;
    for (int k = margin; k + margin < n; ++k) {
        for (int i = margin; i + margin < n; ++i) {
            const auto a = static_cast<std::size_t>(k * n + i);
            const auto b = static_cast<std::size_t>((k + lz) * n + i + lx);
            if (f.face[a] != 0 && f.face[b] != 0) {
                sum += f.detail[a] * f.detail[b];
                na += f.detail[a] * f.detail[a];
                nb += f.detail[b] * f.detail[b];
            }
        }
    }
    return na > 0.0 && nb > 0.0 ? sum / std::sqrt(na * nb) : 0.0;
}

// Along each of 16 directions, how far the correlation of the light rises again over zero (or
// over its lowest at a shorter lag if positive), from 24 to 96 m: a smooth light only decays,
// a single gully swings back from below zero to it (its two flanks, one lit, one in shade), rows
// of spots rise again over zero at their period.
double faceRepeat(const std::vector<std::array<double, 2>>& points, double step, double footprint,
                  double side) {
    std::vector<FaceLight> lights;
    std::size_t faces = 0;
    std::size_t samples = 0;
    for (const std::array<double, 2>& p : points) {
        lights.push_back(faceLight(p[0], p[1], step, footprint, side));
        for (const char c : lights.back().face) {
            faces += c != 0 ? 1 : 0;
        }
        samples += lights.back().face.size();
    }
    const int lags = static_cast<int>(std::lround(96.0 / step));
    const int margin = lags + static_cast<int>(std::lround(40.0 / step)) + 2;
    double most = 0.0;
    for (int a = 0; a < 16; ++a) {
        const double ux = std::cos(a * 0.19634954);
        const double uz = std::sin(a * 0.19634954);
        double lowest = 1.0;
        for (int l = 1; l <= lags; ++l) {
            double v = 0.0;
            for (const FaceLight& f : lights) {
                v += correlation(f, static_cast<int>(std::lround(l * ux)),
                                 static_cast<int>(std::lround(l * uz)), margin) /
                     static_cast<double>(lights.size());
            }
            if (l * step >= 24.0) {
                most = std::max(most, v - std::max(lowest, 0.0));
            }
            lowest = std::min(lowest, v);
        }
    }
    std::printf("%zu faces read every %.0f m at a footprint of %.0f m: %zu of %zu samples steeper "
                "than 20 degrees, their light rises again by %.2f at most past 24 m\n",
                points.size(), step, footprint, faces, samples, most);
    return most;
}

// The ground of the horizon (a footprint of 32 m) against the full detail on the face.
double chuteDifference(double fx, double fz) {
    double most = 0.0;
    for (int k = -32; k < 32; ++k) {
        for (int i = -32; i < 32; ++i) {
            const double x = fx + 32.0 * i;
            const double z = fz + 32.0 * k;
            most = std::max(most, std::abs(dunesAt(x, z, 0.0) - dunesAt(x, z, 32.0)));
        }
    }
    return most;
}

// As the second level of the horizon reads it (every 8 m, a footprint of 32 m), and as the tiles
// and the first level draw it at 1 km (every 2, 4 and 8 m, footprints 0, 0 and 8: the chutes in
// rows of light spots on face_200m, f9c3391).
void testFaceLight() {
    const std::vector<std::array<double, 2>> sites = giantSites(3);
    TEST_CHECK(sites.size() == 3);
    std::vector<std::array<double, 2>> points;
    for (const std::array<double, 2>& site : sites) {
        double fx = 0.0;
        double fz = 0.0;
        if (facePoint(site, fx, fz)) {
            points.push_back({fx, fz});
        }
    }
    TEST_CHECK(points.size() == 3);
    if (points.empty()) {
        return;
    }
    std::printf("face of a giant wave at (%.0f, %.0f); the chutes and all, %.2f m from the "
                "ground of the horizon at most\n",
                points[0][0], points[0][1], chuteDifference(points[0][0], points[0][1]));
    TEST_CHECK(faceRepeat(points, 8.0, 32.0, 1'024.0) <= 0.3);
    TEST_CHECK(faceRepeat(points, 8.0, 8.0, 1'024.0) <= 0.3);
    TEST_CHECK(faceRepeat(points, 4.0, 0.0, 1'024.0) <= 0.3);
    TEST_CHECK(faceRepeat(points, 2.0, 0.0, 1'024.0) <= 0.3);
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
    // And the other way (f9c3391): where the rock stands steep (30 degrees or more) and 6 m or
    // more over the sand draped on it (the ground of the dunes over a flat relief), never the
    // color of the sand (its share 0.01 at most): the drape hides no true rock.
    double hill = 0.0;
    std::size_t hillSand = 0;
    std::size_t rock = 0;
    float rockSand = 0.0f;
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
            if (mask >= 0.99f && d > 0.0 && std::abs(h - last) / 0.5 >= kTan30 &&
                h - dunesAt(x, z, 0.0) >= 6.0) {
                ++rock;
                rockSand = std::max(rockSand, share);
            }
            hillSand += isSand ? 1 : 0;
            last = h;
            lastSand = isSand;
        }
    }
    std::printf("over the buried hill, %zu points of steep rock 6 m over the drape at least, the "
                "share of the sand there %.3f at most\n",
                rock, static_cast<double>(rockSand));
    TEST_CHECK(rock >= 200);
    TEST_CHECK(rockSand <= 0.01f);
    std::printf("sand of %zu ergs of giant waves: %zu points, the steepest %.1f deg; over a buried "
                "hill, %zu points, the steepest %.1f deg\n",
                sites.size(), sand, std::atan(steepest) * dunes_test::kDegrees, hillSand,
                std::atan(hill) * dunes_test::kDegrees);
    TEST_CHECK(hillSand >= 10'000);
    TEST_CHECK(hill <= kTan40);
    TEST_CHECK(sand >= 20'000);
    TEST_CHECK(steepest <= kTan40);
}

// Around the faces of the giant waves of 3 ergs (their backs, their brinks, their slip faces and
// the crescents on them), over 1.5 km every 4 m, the steepest ground in 2D (its gradient from 1 m
// differences, over a flat relief) under 37 degrees: the crescents read where they lie at their
// middle and keep their horns within the room to the next brink (read at the point, their height
// and their width changed within them, and their horns reached over the brinks: 38.9 degrees
// here, 46 on the planet of the game, f9c3391).
void testSteepCrescents() {
    const std::vector<std::array<double, 2>> sites = giantSites(3);
    double steepest = 0.0;
    std::size_t count = 0;
    for (const std::array<double, 2>& site : sites) {
        double fx = 0.0;
        double fz = 0.0;
        if (!facePoint(site, fx, fz)) {
            continue;
        }
        for (int k = -192; k < 192; ++k) {
            for (int i = -192; i < 192; ++i) {
                const double x = fx + 4.0 * i;
                const double z = fz + 4.0 * k;
                const double h = dunesAt(x, z, 0.0);
                const double g =
                    std::hypot(dunesAt(x + 1.0, z, 0.0) - h, dunesAt(x, z + 1.0, 0.0) - h);
                steepest = std::max(steepest, g);
                ++count;
            }
        }
    }
    std::printf("around the faces of the giant waves, %zu samples: the steepest %.1f deg in 2D\n",
                count, std::atan(steepest) * dunes_test::kDegrees);
    TEST_CHECK(count >= 3 * 384 * 384);
    TEST_CHECK(steepest <= kTan37);
}

} // namespace

int main() {
    testFaceLight();
    testSteepSand();
    testSteepCrescents();
    return test::result();
}

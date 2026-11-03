// The crescents laid on the big and giant waves (agent 34, dune_layers.hpp), seen from above
// (the seventh stop: pale commas, all alike, everywhere): over 30 km of forced giant waves and
// 15 km of big waves, sampled every 50 and 25 m, the crescents are their tops on the gentle
// ground (under 8 degrees once smoothed over 300 m, 17 on the giant waves whose backs they cover
// up to 16): what stands out of that smoothed ground by 2 m, the highest of the 5 x 5 samples around
// it, 1.5 m over the mean of the 9 x 9 (not the broad back of a wave) and 1 m over the samples 4
// away in each of 8 directions (a bump, not the rounded crest of a wave along it). In blocks of
// 3 km (big waves: 1.5 km):
// - voids: a quarter of the blocks at least hold one top at most (a stray top on the rounded crest
//   of a wave may pass for one); on the giant waves (the eighth stop: their flanks covered, the
//   voids in their corridors), counting the tops of their corridors alone (the ground smoothed
//   over 300 m in the lowest quarter of its range within 3 km);
// - groups: the densest tenth of the blocks hold three times as many as the median one at least;
//   on the giant waves, their flanks and crests (over half of that range) hold three times as many
//   per square kilometer as their corridors at least, and 4 per square kilometer at least (67fd069:
//   2.2, against 0.3 in the corridors);
// - sizes: the heights of their tops vary by five times at least (90th over 10th percentile); on
//   the giant waves four times (since the eighth stop the big ones cover their flanks: 6.9 to
//   29.3 m; at 50 m the grid misses the smallest).
// Before (44292cd): giant waves 11, 17 and 36 tops per block (25th, 50th and 90th percentiles),
// big waves 3, 6 and 14; tops 3.2 to 13.4 m on the giant waves.
#include "dunes_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

using dunes_test::real;
using terrain::ErgCharacter;

struct Grid {
    int n = 0;
    double step = 0.0;
    std::vector<double> h;
    [[nodiscard]] double at(int i, int k) const {
        return h[static_cast<std::size_t>(std::clamp(k, 0, n - 1) * n + std::clamp(i, 0, n - 1))];
    }
};

Grid sample(double x0, double z0, int n, double step, ErgCharacter c) {
    Grid g{n, step, std::vector<double>(static_cast<std::size_t>(n * n))};
    std::vector<std::thread> threads;
    const int count = dunes_test::threads();
    for (int t = 0; t < count; ++t) {
        threads.emplace_back([&, t] {
            for (int k = t; k < n; k += count) {
                for (int i = 0; i < n; ++i) {
                    g.h[static_cast<std::size_t>(k * n + i)] = dunes_test::forcedDunes(
                        real().ergs, x0 + i * step, z0 + k * step, 0.6f, 0.1f, c);
                }
            }
        });
    }
    for (std::thread& t : threads) {
        t.join();
    }
    return g;
}

// The mean of the grid over a square of 2 r + 1 samples around each (summed areas).
std::vector<double> smoothed(const Grid& g, int r) {
    const int n = g.n;
    std::vector<double> sum(static_cast<std::size_t>((n + 1) * (n + 1)), 0.0);
    for (int k = 0; k < n; ++k) {
        for (int i = 0; i < n; ++i) {
            sum[static_cast<std::size_t>((k + 1) * (n + 1) + i + 1)] =
                g.at(i, k) + sum[static_cast<std::size_t>(k * (n + 1) + i + 1)] +
                sum[static_cast<std::size_t>((k + 1) * (n + 1) + i)] -
                sum[static_cast<std::size_t>(k * (n + 1) + i)];
        }
    }
    std::vector<double> out(g.h.size());
    for (int k = 0; k < n; ++k) {
        for (int i = 0; i < n; ++i) {
            const int i0 = std::max(0, i - r);
            const int i1 = std::min(n, i + r + 1);
            const int k0 = std::max(0, k - r);
            const int k1 = std::min(n, k + r + 1);
            const auto s = [&](int a, int b) { return sum[static_cast<std::size_t>(b * (n + 1) + a)]; };
            out[static_cast<std::size_t>(k * n + i)] =
                (s(i1, k1) - s(i0, k1) - s(i1, k0) + s(i0, k0)) / ((i1 - i0) * (k1 - k0));
        }
    }
    return out;
}

// Where each sample lies in the range of the grid within r samples of it: 0 at its lowest, 1 at
// its highest (separable running minimum and maximum).
std::vector<double> rangePosition(const std::vector<double>& v, int n, int r) {
    std::vector<double> lo(v.size());
    std::vector<double> hi(v.size());
    std::vector<double> lo2(v.size());
    std::vector<double> hi2(v.size());
    const auto at = [n](int i, int k) { return static_cast<std::size_t>(k * n + i); };
    for (int k = 0; k < n; ++k) {
        for (int i = 0; i < n; ++i) {
            double a = v[at(i, k)];
            double b = a;
            for (int d = std::max(0, i - r); d <= std::min(n - 1, i + r); ++d) {
                a = std::min(a, v[at(d, k)]);
                b = std::max(b, v[at(d, k)]);
            }
            lo[at(i, k)] = a;
            hi[at(i, k)] = b;
        }
    }
    std::vector<double> out(v.size());
    for (int k = 0; k < n; ++k) {
        for (int i = 0; i < n; ++i) {
            double a = lo[at(i, k)];
            double b = hi[at(i, k)];
            for (int d = std::max(0, k - r); d <= std::min(n - 1, k + r); ++d) {
                a = std::min(a, lo[at(i, d)]);
                b = std::max(b, hi[at(i, d)]);
            }
            out[at(i, k)] = b > a ? (v[at(i, k)] - a) / (b - a) : 0.5;
        }
    }
    return out;
}

// A top of a crescent at (i, k) of what stands out of the smoothed ground (out): 2 m over it, the
// highest of the 5 x 5 samples around, 1.5 m over the mean of the 9 x 9 and 1 m over the samples
// 4 away in each of 8 directions.
template <typename Out>
bool isTop(const Out& out, int i, int k) {
    const double o = out(i, k);
    bool top = o > 2.0;
    for (int dk = -2; dk <= 2 && top; ++dk) {
        for (int di = -2; di <= 2 && top; ++di) {
            top = (di == 0 && dk == 0) || out(i + di, k + dk) < o;
        }
    }
    double ring = 0.0;
    for (int dk = -4; dk <= 4 && top; ++dk) {
        for (int di = -4; di <= 4; ++di) {
            ring += out(i + di, k + dk) / 81.0;
        }
    }
    for (int d = 0; d < 8 && top; ++d) {
        const int di = d < 3 ? -4 : (d < 5 ? 0 : 4);
        const int dk = d % 3 == 0 ? -4 : (d % 3 == 1 ? 0 : 4);
        top = (di == 0 && dk == 0) || o - out(i + di, k + dk) > 1.0;
    }
    return top && o - ring > 1.5;
}

void testCrescents(ErgCharacter c, double x0, double z0, int n, double step, int block) {
    const bool giant = c == ErgCharacter::Giants;
    const Grid g = sample(x0, z0, n, step, c);
    const int r = static_cast<int>(std::lround(150.0 / step));
    const std::vector<double> smooth = smoothed(g, r);
    const auto smoothAt = [&](int i, int k) {
        return smooth[static_cast<std::size_t>(std::clamp(k, 0, n - 1) * n + std::clamp(i, 0, n - 1))];
    };
    const auto out = [&](int i, int k) { return g.at(i, k) - smoothAt(i, k); };
    const std::vector<double> place =
        rangePosition(smooth, n, static_cast<int>(std::lround(3'000.0 / step)));
    double corridorGround = 0.0;
    double flankGround = 0.0;
    double corridorTops = 0.0;
    double flankTops = 0.0;
    const int blocks = n / block;
    std::vector<int> gentle(static_cast<std::size_t>(blocks * blocks), 0);
    std::vector<int> crescent(gentle.size(), 0);
    std::vector<double> tops;
    for (int k = 1; k + 1 < blocks * block; ++k) {
        for (int i = 1; i + 1 < blocks * block; ++i) {
            const double dx = (smoothAt(i + 1, k) - smoothAt(i - 1, k)) / (2.0 * step);
            const double dz = (smoothAt(i, k + 1) - smoothAt(i, k - 1)) / (2.0 * step);
            if (std::atan(std::hypot(dx, dz)) * dunes_test::kDegrees >= (giant ? 17.0 : 8.0)) {
                continue;
            }
            const double where = place[static_cast<std::size_t>(k * n + i)];
            const bool corridor = where < 0.25;
            const bool flank = where > 0.5;
            corridorGround += corridor ? 1.0 : 0.0;
            flankGround += flank ? 1.0 : 0.0;
            const auto b = static_cast<std::size_t>((k / block) * blocks + i / block);
            gentle[b] += !giant || corridor ? 1 : 0;
            if (isTop(out, i, k)) {
                tops.push_back(out(i, k));
                crescent[b] += !giant || corridor ? 1 : 0;
                corridorTops += corridor ? 1.0 : 0.0;
                flankTops += flank ? 1.0 : 0.0;
            }
        }
    }
    std::vector<double> counts;
    for (std::size_t b = 0; b < gentle.size(); ++b) {
        if (gentle[b] >= block * block / (giant ? 10 : 4)) {
            counts.push_back(crescent[b]);
        }
    }
    std::sort(tops.begin(), tops.end());
    const double p25 = counts.empty() ? 0.0 : dunes_test::quantile(counts, 0.25);
    const double p50 = counts.empty() ? 0.0 : dunes_test::quantile(counts, 0.5);
    const double p90b = counts.empty() ? 0.0 : dunes_test::quantile(counts, 0.9);
    const double p10 = tops.empty() ? 0.0 : dunes_test::quantile(tops, 0.1);
    const double p90 = tops.empty() ? 0.0 : dunes_test::quantile(tops, 0.9);
    std::printf("%s: %zu blocks of %.1f km, tops per block p25 %.0f p50 %.0f p90 %.0f; %zu "
                "crescents, tops p10 %.1f p90 %.1f m\n",
                c == ErgCharacter::Giants ? "giant waves" : "big waves", counts.size(),
                block * step / 1000.0, p25, p50, p90b, tops.size(), p10, p90);
    const double km2 = step * step / 1e6;
    const double corridorDensity = corridorTops / std::max(corridorGround * km2, 1e-9);
    const double flankDensity = flankTops / std::max(flankGround * km2, 1e-9);
    if (giant) {
        std::printf("giant waves: tops per square kilometer, flanks and crests %.2f, corridors %.2f\n",
                    flankDensity, corridorDensity);
    }
    TEST_CHECK(counts.size() >= 20);
    TEST_CHECK(p25 <= 1.0);
    TEST_CHECK(giant ? flankDensity >= 3.0 * corridorDensity && flankDensity >= 4.0
                     : p90b >= 3.0 * std::max(p50, 1.0));
    TEST_CHECK(tops.size() >= 30 && p90 >= (giant ? 4.0 : 5.0) * p10);
}

} // namespace

int main() {
    testCrescents(ErgCharacter::Giants, 1'000'000.0, 2'000'000.0, 600, 50.0, 60);
    testCrescents(ErgCharacter::Waves, -3'000'000.0, 1'500'000.0, 600, 25.0, 60);
    return test::result();
}

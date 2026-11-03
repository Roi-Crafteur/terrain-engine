// The crescents laid on the big and giant waves (agent 34, dune_layers.hpp), seen from above
// (the seventh stop: pale commas, all alike, everywhere): over 30 km of forced giant waves and
// 15 km of big waves, sampled every 50 and 25 m, the crescents are their tops on the gentle
// ground (under 8 degrees once smoothed over 300 m: they lie on the gentle parts of the waves
// only): what stands out of that smoothed ground by 2 m, the highest of the 5 x 5 samples around
// it, 1.5 m over the mean of the 9 x 9 (not the broad back of a wave) and 1 m over the samples 4
// away in each of 8 directions (a bump, not the rounded crest of a wave along it). In blocks of
// 3 km (big waves: 1.5 km):
// - voids: a quarter of the blocks at least hold one top at most (a stray top on the rounded crest
//   of a wave may pass for one);
// - groups: the densest tenth of the blocks hold three times as many as the median one at least;
// - sizes: the heights of their tops vary by five times at least (90th over 10th percentile).
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

void testCrescents(ErgCharacter c, double x0, double z0, int n, double step, int block) {
    const Grid g = sample(x0, z0, n, step, c);
    const int r = static_cast<int>(std::lround(150.0 / step));
    const std::vector<double> smooth = smoothed(g, r);
    const auto smoothAt = [&](int i, int k) {
        return smooth[static_cast<std::size_t>(std::clamp(k, 0, n - 1) * n + std::clamp(i, 0, n - 1))];
    };
    const auto out = [&](int i, int k) { return g.at(i, k) - smoothAt(i, k); };
    const int blocks = n / block;
    std::vector<int> gentle(static_cast<std::size_t>(blocks * blocks), 0);
    std::vector<int> crescent(gentle.size(), 0);
    std::vector<double> tops;
    for (int k = 1; k + 1 < blocks * block; ++k) {
        for (int i = 1; i + 1 < blocks * block; ++i) {
            const double dx = (smoothAt(i + 1, k) - smoothAt(i - 1, k)) / (2.0 * step);
            const double dz = (smoothAt(i, k + 1) - smoothAt(i, k - 1)) / (2.0 * step);
            if (std::atan(std::hypot(dx, dz)) * dunes_test::kDegrees >= 8.0) {
                continue;
            }
            const auto b = static_cast<std::size_t>((k / block) * blocks + i / block);
            ++gentle[b];
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
            if (top && o - ring > 1.5) {
                tops.push_back(o);
                ++crescent[b];
            }
        }
    }
    std::vector<double> counts;
    for (std::size_t b = 0; b < gentle.size(); ++b) {
        if (gentle[b] >= block * block / 4) {
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
    TEST_CHECK(counts.size() >= 20);
    TEST_CHECK(p25 <= 1.0);
    TEST_CHECK(p90b >= 3.0 * std::max(p50, 1.0));
    TEST_CHECK(tops.size() >= 30 && p90 >= 5.0 * p10);
}

} // namespace

int main() {
    testCrescents(ErgCharacter::Giants, 1'000'000.0, 2'000'000.0, 600, 50.0, 60);
    testCrescents(ErgCharacter::Waves, -3'000'000.0, 1'500'000.0, 600, 25.0, 60);
    return test::result();
}

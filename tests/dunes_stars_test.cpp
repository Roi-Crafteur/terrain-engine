// The star dunes are no pyramids (agent 34, the stars at 1 km; dune_stars.cpp): their arms curl,
// their crests rise and fall, no two neighbours alike.
// - From the ground alone (whatever draws the stars): around each summit, the arms are traced as
//   the crests met on circles 25 m apart. In plan, the crest of an arm strays from the straight
//   line between its ends (median over the arms at least kPlanMedian of that line, its tenth at
//   least kPlanLeast); in height, a share of the arms rises again after a saddle (a secondary
//   summit) and the summits of neighbours vary in height and in the number of their arms.
// - From the crests of the free stars (dune_forms::starFormOf): every arm strays in plan and in
//   height (of its top) from the straight line between its ends, the tallest stars carry a secondary summit on
//   an arm, the arms of a star are of uneven lengths, and the ground stands on the crests.
#include "dune_forms.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

namespace forms = terrain::dune_forms;

constexpr double kPi = 3.141592653589793;
constexpr double kPlanMedian = 0.06;
constexpr double kPlanLeast = 0.015;

forms::StarLattice lattice() {
    forms::StarLattice s;
    s.periodX = 32.0 * 65536.0;
    s.periodZ = 16.0 * 65536.0;
    s.height = 200.0;
    s.occupancy = 0.8;
    s.key = 42;
    return s;
}

double variation(const std::vector<double>& v) {
    double mean = 0.0;
    for (const double x : v) {
        mean += x / static_cast<double>(v.size());
    }
    double var = 0.0;
    for (const double x : v) {
        var += (x - mean) * (x - mean) / static_cast<double>(v.size());
    }
    return std::sqrt(var) / mean;
}

// The summits over 20 x 10 km: maxima of a 40 m grid over 60 m, the highest within 800 m.
std::vector<std::array<double, 3>> summits(const forms::StarLattice& s) {
    constexpr int nx = 500;
    constexpr int nz = 250;
    constexpr double step = 40.0;
    std::vector<double> g(static_cast<std::size_t>(nx * nz));
    for (int j = 0; j < nz; ++j) {
        for (int i = 0; i < nx; ++i) {
            g[static_cast<std::size_t>(j * nx + i)] = forms::stars(s, step * i, step * j);
        }
    }
    std::vector<std::array<double, 3>> out;
    for (int j = 20; j < nz - 20; ++j) {
        for (int i = 20; i < nx - 20; ++i) {
            const double c = g[static_cast<std::size_t>(j * nx + i)];
            bool top = c >= 60.0;
            for (int dj = -20; dj <= 20 && top; ++dj) {
                for (int di = -20; di <= 20 && top; ++di) {
                    top = di * di + dj * dj > 400 ||
                          g[static_cast<std::size_t>((j + dj) * nx + i + di)] <= c;
                }
            }
            if (top) {
                out.push_back({step * i, step * j, c});
            }
        }
    }
    return out;
}

struct Track {
    std::vector<std::array<double, 3>> p;
    double angle = 0.0;
    bool open = true;
};

// The crests met on a circle of radius r around (cx, cz): local maxima over 5 m, 1.5 m over the
// ground within 4 degrees. Their angles and heights.
std::vector<std::array<double, 2>> crestsOn(const forms::StarLattice& s, double cx, double cz,
                                            double r) {
    constexpr int m = 720;
    std::vector<double> v(m);
    for (int k = 0; k < m; ++k) {
        const double a = 2.0 * kPi * k / m;
        v[static_cast<std::size_t>(k)] = forms::stars(s, cx + r * std::cos(a), cz + r * std::sin(a));
    }
    const auto at = [&](int k) { return v[static_cast<std::size_t>((k + m) % m)]; };
    std::vector<std::array<double, 2>> out;
    for (int k = 0; k < m; ++k) {
        const double c = at(k);
        double low = c;
        for (int d = -8; d <= 8; ++d) {
            low = std::min(low, at(k + d));
        }
        if (c > 5.0 && c > at(k - 1) && c >= at(k + 1) && c - low > 1.5) {
            out.push_back({2.0 * kPi * k / m, c});
        }
    }
    return out;
}

// The arms of the star of a summit: the crests followed outward, 40 m apart at most from one
// circle to the next, started near its summit, kept over 200 m.
std::vector<Track> armsOf(const forms::StarLattice& s, const std::array<double, 3>& top) {
    std::vector<Track> tracks;
    const double first = 0.5 * top[2];
    for (double r = first; r < 1400.0; r += 25.0) {
        const std::vector<std::array<double, 2>> crests = crestsOn(s, top[0], top[1], r);
        std::vector<bool> used(crests.size(), false);
        for (Track& t : tracks) {
            std::size_t best = crests.size();
            double near = 40.0;
            for (std::size_t c = 0; c < crests.size(); ++c) {
                const double d = std::abs(std::remainder(crests[c][0] - t.angle, 2.0 * kPi)) * r;
                if (!used[c] && t.open && d < near) {
                    near = d;
                    best = c;
                }
            }
            t.open = best < crests.size();
            if (t.open) {
                used[best] = true;
                t.angle = crests[best][0];
                t.p.push_back({top[0] + r * std::cos(t.angle), top[1] + r * std::sin(t.angle),
                               crests[best][1]});
            }
        }
        for (std::size_t c = 0; c < crests.size() && r <= first + 75.0; ++c) {
            if (!used[c]) {
                tracks.push_back({{{top[0] + r * std::cos(crests[c][0]),
                                    top[1] + r * std::sin(crests[c][0]), crests[c][1]}},
                                  crests[c][0], true});
            }
        }
    }
    std::erase_if(tracks, [](const Track& t) { return t.p.size() < 8; });
    return tracks;
}

// How far points (x, z) stray from the straight line between the first and the last, over its
// length.
template <class P> double strayOf(const std::vector<P>& p, auto x, auto z) {
    const double dx = x(p.back()) - x(p.front());
    const double dz = z(p.back()) - z(p.front());
    const double length = std::hypot(dx, dz);
    double most = 0.0;
    for (const P& q : p) {
        most = std::max(most, std::abs((x(q) - x(p.front())) * dz - (z(q) - z(p.front())) * dx) /
                                  length);
    }
    return most / length;
}

void testFromTheGround() {
    const forms::StarLattice s = lattice();
    std::vector<double> stray;
    std::vector<double> peaks;
    std::vector<double> counts;
    std::size_t rising = 0;
    for (const std::array<double, 3>& top : summits(s)) {
        const std::vector<Track> arms = armsOf(s, top);
        peaks.push_back(top[2]);
        counts.push_back(static_cast<double>(arms.size()));
        for (const Track& t : arms) {
            stray.push_back(strayOf(t.p, [](const auto& q) { return q[0]; },
                                    [](const auto& q) { return q[1]; }));
            double low = 1.0e9;
            bool rises = false;
            for (const auto& q : t.p) {
                rises = rises || q[2] > low + 2.0;
                low = std::min(low, q[2]);
            }
            rising += rises ? 1 : 0;
        }
    }
    std::sort(stray.begin(), stray.end());
    const double median = stray.empty() ? 0.0 : stray[stray.size() / 2];
    const double least = stray.empty() ? 0.0 : stray[stray.size() / 10];
    const double rise = stray.empty() ? 0.0 : static_cast<double>(rising) / static_cast<double>(stray.size());
    std::printf("stars from the ground: %zu summits, %zu arms; in plan their crests stray by %.3f "
                "(median, tenth %.3f) of their length; %.0f %% rise again after a saddle; the "
                "summits vary by %.2f in height, by %.2f in arms\n",
                peaks.size(), stray.size(), median, least, 100.0 * rise, variation(peaks),
                variation(counts));
    TEST_CHECK(peaks.size() >= 15 && stray.size() >= 3 * peaks.size());
    TEST_CHECK(median >= kPlanMedian && least >= kPlanLeast);
    TEST_CHECK(rise >= 0.25);
    TEST_CHECK(variation(peaks) >= 0.2 && variation(counts) >= 0.15);
}

// The crests of the free stars of 40 cells.
void testCrests() {
    const forms::StarLattice s = lattice();
    double plan = 1.0;
    double height = 1.0;
    double lengths = 0.0;
    double below = 0.0;
    std::size_t tall = 0;
    std::size_t second = 0;
    std::size_t stars = 0;
    for (int k = 0; k < 40; ++k) {
        const forms::StarForm f = forms::starFormOf(s, 100 + k % 8, 50 + k / 8);
        if (f.arms.empty()) {
            continue;
        }
        ++stars;
        std::vector<double> length;
        bool summit = false;
        for (const std::vector<forms::StarCrest>& arm : f.arms) {
            plan = std::min(plan, strayOf(arm, [](const auto& q) { return q.x; },
                                          [](const auto& q) { return q.z; }));
            double run = 0.0;
            double off = 0.0;
            for (std::size_t n = 0; n < arm.size(); ++n) {
                run += n > 0 ? std::hypot(arm[n].x - arm[n - 1].x, arm[n].z - arm[n - 1].z) : 0.0;
                const double u = static_cast<double>(n) / static_cast<double>(arm.size() - 1);
                off = std::max(off, std::abs(arm[n].height - (1.0 - u) * arm.front().height -
                                             u * arm.back().height));
                summit = summit || (n > 0 && n + 1 < arm.size() &&
                                    arm[n].height > arm[n - 1].height + 1.0 &&
                                    arm[n].height > arm[n + 1].height);
                below = std::max(below, arm[n].height - forms::stars(s, arm[n].x, arm[n].z));
            }
            height = std::min(height, off / arm.front().height);
            length.push_back(run);
        }
        lengths += variation(length);
        tall += f.peak > 0.7 * 1.2 * s.height ? 1 : 0;
        second += f.peak > 0.7 * 1.2 * s.height && summit ? 1 : 0;
    }
    lengths /= static_cast<double>(stars);
    std::printf("stars from their crests: %zu stars, every arm strays by %.3f of its length in "
                "plan at least, by %.3f of its top in height; the lengths of their arms vary by "
                "%.2f; %zu of the %zu tallest carry a secondary summit; the ground at most %.2f m "
                "under the crests\n",
                stars, plan, height, lengths, second, tall, below);
    TEST_CHECK(stars >= 20);
    TEST_CHECK(plan >= 0.02 && height >= 0.04);
    TEST_CHECK(lengths >= 0.15);
    TEST_CHECK(tall >= 3 && second == tall);
    TEST_CHECK(below <= 0.01);
}

} // namespace

int main() {
    testFromTheGround();
    testCrests();
    return test::result();
}

// The edges of the plateaus (agent 46, tableland_relief.hpp) on the default world switched on, read
// along their lines (pattern_support.hpp):
//   - straightness: the rim of the escarpment (the zero line of Fields::scarp, followed 25 m at a
//     time) around dry edges and coastal ones (the province faded by its coast); the longest
//     stretch of it within 50 m of its chord stays under 2 km;
//   - continuity: where the share of the province vanishes, the largest step of the heights over
//     25 m is no larger than that of the world switched off, 5 m more at most;
//   - no comb: along lines parallel to the edge of the province (its share 0.6, followed 100 m at a
//     time) and 1.5 km on each side of them, the crossings of tops and of low ground are of very
//     varied widths (from a few hundred meters to kilometers) and repeat at no spacing.
#include "pattern_support.hpp"
#include "tablelands_fields_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using tablelands_fields::fieldsAt;
using tablelands_fields::heightAt;
using tablelands_fields::provinceAt;

struct Point {
    double x = 0.0;
    double z = 0.0;
};

// Straightness: kTolerance meters off the chord, kStraightMost meters at most; the rims followed
// kRimStep meters at a time over kRimLength meters on each side of their start.
constexpr double kTolerance = 50.0;
constexpr double kStraightMost = 2'000.0;
constexpr double kRimStep = 25.0;
constexpr double kRimLength = 15'000.0;
// Continuity: kBox meters around the vanishing edge, read every kPair meters; kStepMore meters
// more than the world switched off at most.
constexpr double kBox = 2'000.0;
constexpr double kPair = 25.0;
constexpr double kStepMore = 5.0;
// The comb: lines along the share kEdgeShare of the province followed kEdgeStep meters at a time
// over kEdgeLength meters, read every kRead meters at kOffsets; the crossings of kLeastCrossing
// meters or more of width varying by kLeastVariation at least, their 90th percentile kLeastSpread
// times their 10th, from kSmallest meters or less to kLargest or more; no repetition (the
// correlation of the profile less its mean over kDetrend meters, under kPeriodicMost at the lags of
// kLagLow to kLagHigh meters).
constexpr double kEdgeShare = 0.6;
constexpr double kEdgeStep = 100.0;
constexpr double kEdgeLength = 30'000.0;
constexpr double kRead = 25.0;
constexpr std::array<double, 3> kOffsets = {-1'500.0, 0.0, 1'500.0};
constexpr double kLeastVariation = 0.6;
constexpr double kLeastSpread = 5.0;
constexpr double kSmallest = 300.0;
constexpr double kLargest = 3'000.0;
constexpr double kLeastCrossing = 100.0;
constexpr double kPeriodicMost = 0.5;
constexpr double kDetrend = 5'000.0;
constexpr double kLagLow = 1'000.0;
constexpr double kLagHigh = 5'000.0;

// The gradient of f at p by differences of h meters.
template <typename F>
Point gradientOf(const F& f, const Point& p, double h) {
    const double v = f(p.x, p.z);
    return {(f(p.x + h, p.z) - v) / h, (f(p.x, p.z + h) - v) / h};
}

// The zero line of f from start, step meters at a time, length meters in each direction (no
// farther where it is lost: its value tolerance or more off zero after two projections).
template <typename F>
std::vector<Point> zeroLine(const F& f, const Point& start, double step, double length, double h,
                            double tolerance) {
    std::vector<Point> out;
    for (const double way : {-1.0, 1.0}) {
        std::vector<Point> half;
        Point p = start;
        Point g = gradientOf(f, p, h);
        Point last{0.0, 0.0};
        for (double run = 0.0; run < length; run += step) {
            const double norm = std::hypot(g.x, g.z);
            if (!(norm > 0.0)) {
                break;
            }
            Point t{-g.z / norm * way, g.x / norm * way};
            if (run > 0.0 && t.x * last.x + t.z * last.z < 0.0) {
                t = {-t.x, -t.z};
            }
            last = t;
            Point q{p.x + step * t.x, p.z + step * t.z};
            for (int k = 0; k < 2; ++k) {
                g = gradientOf(f, q, h);
                const double n2 = g.x * g.x + g.z * g.z;
                const double v = f(q.x, q.z);
                if (!(n2 > 0.0)) {
                    break;
                }
                q = {q.x - v * g.x / n2, q.z - v * g.z / n2};
            }
            if (!(std::abs(f(q.x, q.z)) < tolerance)) {
                break;
            }
            half.push_back(q);
            p = q;
        }
        if (way < 0.0) {
            out.assign(half.rbegin(), half.rend());
            out.push_back(start);
        } else {
            out.insert(out.end(), half.begin(), half.end());
        }
    }
    return out;
}

// A point on the zero line of f near site: along x, then z, the first change of sign within reach
// meters, refined by bisection.
template <typename F>
bool zeroNear(const F& f, const Point& site, double reach, Point& out) {
    for (const Point d : {Point{1.0, 0.0}, Point{0.0, 1.0}, Point{-1.0, 0.0}, Point{0.0, -1.0}}) {
        double a = 0.0;
        const double va = f(site.x, site.z);
        for (double b = 250.0; b <= reach; b += 250.0) {
            const double vb = f(site.x + d.x * b, site.z + d.z * b);
            if ((va > 0.0) == (vb > 0.0)) {
                a = b;
                continue;
            }
            double lo = a;
            double hi = b;
            for (int k = 0; k < 30; ++k) {
                const double mid = 0.5 * (lo + hi);
                const double vm = f(site.x + d.x * mid, site.z + d.z * mid);
                ((vm > 0.0) == (va > 0.0) ? lo : hi) = mid;
            }
            out = {site.x + d.x * lo, site.z + d.z * lo};
            return true;
        }
    }
    return false;
}

// The longest stretch of a line of points spaced spacing meters apart within kTolerance of its
// chord, meters.
double longestStraight(const std::vector<Point>& line, double spacing) {
    std::size_t best = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        std::size_t j = i + 2;
        for (; j < line.size(); ++j) {
            const double cx = line[j].x - line[i].x;
            const double cz = line[j].z - line[i].z;
            const double chord = std::hypot(cx, cz);
            bool within = chord > 0.0;
            for (std::size_t k = i + 1; k < j && within; ++k) {
                const double off =
                    std::abs((line[k].x - line[i].x) * cz - (line[k].z - line[i].z) * cx) / chord;
                within = off <= kTolerance;
            }
            if (!within) {
                break;
            }
        }
        best = std::max(best, j - 1 - i);
    }
    return spacing * static_cast<double>(best);
}

void testStraightness(const tablelands_fields::Sites& sites) {
    const auto scarp = [](double x, double z) { return fieldsAt(x, z).scarp; };
    double traced = 0.0;
    double longest = 0.0;
    int rims = 0;
    std::vector<std::array<double, 2>> starts(sites.edges.begin(),
                                              sites.edges.begin() +
                                                  static_cast<long>(std::min<std::size_t>(5, sites.edges.size())));
    for (std::size_t c = 0; c < sites.coastal.size() && c < 2; ++c) {
        starts.push_back(sites.coastal[c]);
    }
    for (std::size_t s = 0; s < starts.size(); ++s) {
        Point start;
        if (!zeroNear(scarp, {starts[s][0], starts[s][1]}, 30'000.0, start)) {
            continue;
        }
        const std::vector<Point> rim = zeroLine(scarp, start, kRimStep, kRimLength, 5.0, 5.0);
        const double straight = longestStraight(rim, kRimStep);
        traced += kRimStep * static_cast<double>(rim.size());
        longest = std::max(longest, straight);
        ++rims;
        std::printf("rim near (%.0f, %.0f)%s: %.1f km followed, its longest straight stretch "
                    "(within 50 m) %.0f m\n",
                    start.x, start.z, s >= std::min<std::size_t>(5, sites.edges.size()) ? " (coastal)" : "",
                    kRimStep * static_cast<double>(rim.size()) / 1000.0, straight);
    }
    std::printf("straightness: %d rims, %.0f km followed, the longest straight stretch %.0f m (under "
                "%.0f m)\n",
                rims, traced / 1000.0, longest, kStraightMost);
    TEST_CHECK(traced >= 100'000.0);
    TEST_CHECK(longest < kStraightMost);
}

// The largest step of the heights of f between points kPair meters apart in a box kBox meters wide
// around c.
double largestStep(const terrain::Heightfield& f, const Point& c) {
    const int n = static_cast<int>(kBox / kPair);
    std::vector<double> row(static_cast<std::size_t>(n + 1));
    std::vector<double> previous(static_cast<std::size_t>(n + 1));
    double most = 0.0;
    for (int j = 0; j <= n; ++j) {
        for (int i = 0; i <= n; ++i) {
            const auto k = static_cast<std::size_t>(i);
            row[k] = static_cast<double>(
                f.sampleAt(c.x - 0.5 * kBox + kPair * i, c.z - 0.5 * kBox + kPair * j).height);
            most = i > 0 ? std::max(most, std::abs(row[k] - row[k - 1])) : most;
            most = j > 0 ? std::max(most, std::abs(row[k] - previous[k])) : most;
        }
        std::swap(row, previous);
    }
    return most;
}

void testContinuity(const tablelands_fields::Sites& sites) {
    const terrain::Heightfield off(terrain::kDefaultWorldSeed);
    const auto held = [](double x, double z) {
        return provinceAt(x, z).point.tableland > 0.0f ? 1.0 : -1.0;
    };
    int edges = 0;
    for (std::size_t m = 0; m < sites.margins.size() && edges < 3; ++m) {
        Point edge;
        if (!zeroNear(held, {sites.margins[m][0], sites.margins[m][1]}, 30'000.0, edge)) {
            continue;
        }
        const double on = largestStep(tablelands_fields::field(), edge);
        const double without = largestStep(off, edge);
        std::printf("where the province vanishes near (%.0f, %.0f): the largest step over 25 m "
                    "%.1f m, %.1f m switched off\n",
                    edge.x, edge.z, on, without);
        TEST_CHECK(on <= without + kStepMore);
        ++edges;
    }
    TEST_CHECK(edges == 3);
}

// The points of a line every kRead meters, offset meters along its normal.
std::vector<Point> readAlong(const std::vector<Point>& line, double offset) {
    std::vector<Point> out;
    for (std::size_t i = 1; i < line.size(); ++i) {
        const double dx = line[i].x - line[i - 1].x;
        const double dz = line[i].z - line[i - 1].z;
        const double length = std::hypot(dx, dz);
        if (!(length > 0.0)) {
            continue;
        }
        for (double s = 0.0; s < length; s += kRead) {
            out.push_back({line[i - 1].x + dx * s / length - dz / length * offset,
                           line[i - 1].z + dz * s / length + dx / length * offset});
        }
    }
    return out;
}

// The widths of the runs of tops and of low ground along a line, and how periodic they are.
double addCrossings(const std::vector<Point>& points, std::vector<double>& widths) {
    std::vector<double> top;
    std::vector<double> low;
    for (const Point& p : points) {
        const double t = fieldsAt(p.x, p.z).profile >= 0.5 ? 1.0 : 0.0;
        top.push_back(t);
        low.push_back(1.0 - t);
    }
    for (const std::vector<double>* runs : {&top, &low}) {
        const pattern_test::Grooves g = pattern_test::groovesOf(*runs, kRead, 0.5);
        for (const double w : g.widths) {
            if (w >= kLeastCrossing) {
                widths.push_back(w);
            }
        }
    }
    return pattern_test::peakCorrelation(
        pattern_test::detrended(top, static_cast<std::size_t>(kDetrend / kRead)),
        static_cast<std::size_t>(kLagLow / kRead), static_cast<std::size_t>(kLagHigh / kRead));
}

void testComb(const tablelands_fields::Sites& sites) {
    const auto share = [](double x, double z) {
        return static_cast<double>(provinceAt(x, z).point.tableland) - kEdgeShare;
    };
    std::vector<double> widths;
    double periodic = 0.0;
    int lines = 0;
    for (std::size_t s = 0; s < sites.edges.size() && lines < 9; ++s) {
        Point start;
        if (!zeroNear(share, {sites.edges[s][0], sites.edges[s][1]}, 30'000.0, start)) {
            continue;
        }
        const std::vector<Point> edge = zeroLine(share, start, kEdgeStep, kEdgeLength, 20.0, 0.01);
        for (const double offset : kOffsets) {
            periodic = std::max(periodic, addCrossings(readAlong(edge, offset), widths));
            ++lines;
        }
    }
    std::sort(widths.begin(), widths.end());
    const double variation = pattern_test::variation(widths);
    const double p10 = widths.empty() ? 0.0 : widths[widths.size() / 10];
    const double p90 = widths.empty() ? 0.0 : widths[widths.size() * 9 / 10];
    std::printf("the edge: %d lines, %zu crossings of tops and low ground from %.0f to %.0f m wide "
                "(10th percentile %.0f m, 90th %.0f m), varying by %.2f, the most periodic %.2f\n",
                lines, widths.size(), widths.empty() ? 0.0 : widths.front(),
                widths.empty() ? 0.0 : widths.back(), p10, p90, variation, periodic);
    TEST_CHECK(widths.size() >= 30);
    TEST_CHECK(variation >= kLeastVariation);
    TEST_CHECK(p90 >= kLeastSpread * p10);
    TEST_CHECK(!widths.empty() && widths.front() <= kSmallest && widths.back() >= kLargest);
    TEST_CHECK(periodic < kPeriodicMost);
}

} // namespace

int main() {
    const tablelands_fields::Sites sites = tablelands_fields::findSites();
    TEST_CHECK(!sites.edges.empty() && !sites.coastal.empty());
    testStraightness(sites);
    testContinuity(sites);
    testComb(sites);
    return test::result();
}

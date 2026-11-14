// The edges of the plateaus (agent 46, tableland_relief.hpp) on the default world switched on, read
// along their lines (pattern_support.hpp):
//   - straightness: the rim of the escarpment (the zero line of Fields::scarp, followed 25 m at a
//     time) around dry edges and coastal ones (the province faded by its coast); the longest stretch of it within 50 m of
//     its chord stays under 2 km; and over 20 km of it, it strays 1 km from its chord (the median
//     over its stretches of 20 km), 0.5 km at least over its straightest (no long straight front: a
//     noise has stretches of little curvature, which a full kilometer everywhere would fold);
//   - the fringe of the outliers: its density (the share of the ground 0.5 to 8 km in front of the
//     rim on their tops) over 10 km of the rim, its 90th percentile 4 times its 10th at least;
//   - continuity: where the share of the province vanishes, the largest step of the heights over
//     25 m is no larger than that of the world switched off, 5 m more at most;
//   - no comb: along lines parallel to the edge of the province (its share 0.6, followed 100 m at a
//     time) and 1.5 km on each side of them, the crossings of tops and of low ground are of very
//     varied widths (from a few hundred meters to kilometers) and repeat at no spacing.
#include "pattern_support.hpp"
#include "tablelands_fields_support.hpp"
#include "tablelands_lines_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using tablelands_fields::Fields;
using tablelands_fields::fieldsAt;
using tablelands_fields::heightAt;
using tablelands_fields::provinceAt;
using tablelands_lines::gradientOf;
using tablelands_lines::leastBend;
using tablelands_lines::longestStraight;
using tablelands_lines::Point;
using tablelands_lines::zeroLine;
using tablelands_lines::zeroNear;

// Straightness: kTolerance meters off the chord, kStraightMost meters at most; the rims followed
// kRimStep meters at a time over kRimLength meters on each side of their start.
constexpr double kTolerance = 50.0;
constexpr double kStraightMost = 2'000.0;
constexpr double kRimStep = 25.0;
constexpr double kRimLength = 20'000.0;
// The rims followed: from kDryRims dry edges and kCoastalRims coastal ones.
constexpr std::size_t kDryRims = 8;
constexpr std::size_t kCoastalRims = 3;
// Over kBendWindow meters of a rim, it strays kTypicalBend meters from its chord (the median over
// its stretches), kLeastBend over its straightest.
constexpr double kBendWindow = 20'000.0;
constexpr double kTypicalBend = 1'000.0;
constexpr double kLeastBend = 500.0;
// The fringe: the ground kFringeNear to kFringeFar meters in front of the rim, read every kFringeStep
// meters, every kFringeEvery meters of it; its density over kFringeWindow meters of the rim, the
// 90th percentile kLeastFringeSpread times the 10th at least (no less than kLeastFringe).
constexpr double kFringeNear = 500.0;
constexpr double kFringeFar = 8'000.0;
constexpr double kFringeStep = 250.0;
constexpr double kFringeEvery = 250.0;
constexpr double kFringeWindow = 10'000.0;
constexpr double kLeastFringeSpread = 4.0;
constexpr double kLeastFringe = 0.01;
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

// The escarpment: its rim the zero line.
double scarpOf(double x, double z) {
    return fieldsAt(x, z).scarp;
}

// The density of the fringe in front of a rim (the zero line of scarpOf), over its windows.
void addFringe(const std::vector<Point>& rim, std::vector<double>& densities) {
    const auto every = static_cast<std::size_t>(kFringeEvery / kRimStep);
    const auto per = static_cast<std::size_t>(kFringeWindow / kFringeEvery);
    std::vector<double> shares;
    for (std::size_t i = 0; i < rim.size(); i += every) {
        const Point g = gradientOf(scarpOf, rim[i], 20.0);
        const double norm = std::max(std::hypot(g.x, g.z), 1e-12);
        int on = 0;
        int read = 0;
        for (double d = kFringeNear; d <= kFringeFar; d += kFringeStep) {
            const Fields f = fieldsAt(rim[i].x - g.x / norm * d, rim[i].z - g.z / norm * d);
            on += f.outlier && f.profile > 0.5 ? 1 : 0;
            ++read;
        }
        shares.push_back(static_cast<double>(on) / static_cast<double>(read));
    }
    for (std::size_t w = 0; w + per <= shares.size(); w += per) {
        double sum = 0.0;
        for (std::size_t k = w; k < w + per; ++k) {
            sum += shares[k];
        }
        densities.push_back(sum / static_cast<double>(per));
    }
}

void testStraightness(const tablelands_fields::Sites& sites) {
    double traced = 0.0;
    double longest = 0.0;
    int rims = 0;
    const std::size_t dry = std::min<std::size_t>(kDryRims, sites.edges.size());
    std::vector<std::array<double, 2>> starts(sites.edges.begin(),
                                              sites.edges.begin() + static_cast<long>(dry));
    for (std::size_t c = 0; c < sites.coastal.size() && c < kCoastalRims; ++c) {
        starts.push_back(sites.coastal[c]);
    }
    std::vector<double> densities;
    for (std::size_t s = 0; s < starts.size(); ++s) {
        Point start;
        if (!zeroNear(scarpOf, {starts[s][0], starts[s][1]}, 30'000.0, start)) {
            continue;
        }
        const std::vector<Point> rim = zeroLine(scarpOf, start, kRimStep, kRimLength, 5.0, 10.0);
        Point from = start;
        const double straight = longestStraight(rim, kRimStep, kTolerance, from);
        const double length = kRimStep * static_cast<double>(rim.size());
        double typical = 0.0;
        const double bend = leastBend(rim, kRimStep, kBendWindow, typical);
        traced += length;
        longest = std::max(longest, straight);
        ++rims;
        std::printf("rim near (%.0f, %.0f)%s: %.1f km followed, its longest straight stretch "
                    "(within 50 m) %.0f m from (%.0f, %.0f); over 20 km, %.2f km off its chord at "
                    "the least, %.2f km the median\n",
                    start.x, start.z, s >= dry ? " (coastal)" : "", length / 1000.0, straight, from.x,
                    from.z, bend / 1000.0, typical / 1000.0);
        if (length >= kBendWindow + 1'000.0) {
            TEST_CHECK(typical >= kTypicalBend && bend >= kLeastBend);
        }
        if (s < dry) {
            addFringe(rim, densities);
        }
    }
    std::printf("straightness: %d rims, %.0f km followed, the longest straight stretch %.0f m (under "
                "%.0f m)\n",
                rims, traced / 1000.0, longest, kStraightMost);
    TEST_CHECK(traced >= 100'000.0);
    TEST_CHECK(longest < kStraightMost);
    std::sort(densities.begin(), densities.end());
    const double p10 = densities.empty() ? 0.0 : std::max(densities[densities.size() / 10], kLeastFringe);
    const double p90 = densities.empty() ? 0.0 : densities[densities.size() * 9 / 10];
    std::printf("the fringe: %zu windows of 10 km of the dry rims, its density %.3f to %.3f (10th "
                "percentile %.3f, 90th %.3f: %.1f times)\n",
                densities.size(), densities.empty() ? 0.0 : densities.front(),
                densities.empty() ? 0.0 : densities.back(), p10, p90, p90 / std::max(p10, 1e-9));
    TEST_CHECK(densities.size() >= 8);
    TEST_CHECK(p90 >= kLeastFringeSpread * p10);
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

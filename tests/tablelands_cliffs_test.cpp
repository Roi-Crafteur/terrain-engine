// The cliffs of the buttes of the plateaus (agent 46, tableland_cliffs.hpp) on the default world
// switched on: walls of rock, not cookie cuts (Merrick Butte, not Devils Tower):
//   - their outline rough from 20 to 200 m: the radius of the top of a butte from its middle every
//     degree, less that radius smoothed over 200 m of its rim; its rms kLeastRough meters at least
//     (the median over the buttes);
//   - their batter: the mean angle of their cliff from its rim to its foot over 16 rays (those whose
//     cliff falls half as far as the tallest at least: not a single riser), the median over the buttes from kBatterLow to kBatterHigh degrees, at most kSteepShare of them steeper
//     than kSteepest, their 10th and 90th percentiles kLeastBatterSpread degrees apart at least;
//   - their ledges: within their cliff, runs of kLedgeLeast meters or more under 45 degrees; 1 to 3
//     of them on most of the buttes (the median of their rays), the first one at depths under the
//     rim (shares of the cliff) varying from butte to butte by kLeastLedgeSpread at least;
//   - their scree: the heights of the talus within kScreeReach meters of the foot of the cliff off
//     their mean over kScreeSmooth meters, kLeastScree meters rms (the median); the blocks along the
//     foot without a period (pattern_support.hpp: the peak correlation of that profile under
//     kPeriodicMost).
// The buttes: tablelands_buttes_support.hpp, 80 to 400 m high, their walls steeper than 60 degrees.
#include "pattern_support.hpp"
#include "tablelands_buttes_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using tablelands_buttes::Butte;
using tablelands_buttes::CliffRay;
using tablelands_buttes::cliffAlong;
using tablelands_buttes::kSteep60;
using tablelands_fields::fieldsAt;
using tablelands_fields::heightAt;

constexpr double kPi = 3.141592653589793;
constexpr double kDegrees = 57.29577951308232;
// The buttes of the case (as tablelands_shapes_test), kMostButtes of them at most.
constexpr double kButteLow = 80.0;
constexpr double kButteHigh = 400.0;
constexpr std::size_t kMostButtes = 16;
constexpr int kLeastButtes = 5;
// The outline: radii every degree, sought kRadialStep meters at a time up to kRadiusMost, smoothed
// over kRoughSmooth meters of the rim.
constexpr int kAngles = 360;
constexpr double kRadialStep = 10.0;
constexpr double kRadiusMost = 2'000.0;
constexpr double kRoughSmooth = 200.0;
constexpr double kLeastRough = 10.0;
// The batter and the ledges along kRays rays, kRayStep meters a step; cliffs of kCliffLeast meters
// at least.
constexpr int kRays = 16;
constexpr double kWholeCliff = 0.5;
constexpr double kRayStep = 0.5;
constexpr double kCliffLeast = 30.0;
constexpr double kBatterLow = 80.0;
constexpr double kBatterHigh = 88.0;
constexpr double kSteepest = 89.0;
constexpr double kSteepShare = 0.1;
constexpr double kLeastBatterSpread = 3.0;
constexpr double kLedgeLeast = 2.0;
constexpr double kLedgeSlope = 1.0;
constexpr double kLedgedShare = 0.5;
constexpr double kLeastLedgeSpread = 0.1;
// The scree.
constexpr double kScreeReach = 80.0;
constexpr double kScreeSmooth = 30.0;
constexpr double kLeastScree = 1.0;
constexpr double kFootOut = 10.0;
constexpr double kLagLow = 15.0;
constexpr double kLagHigh = 90.0;
constexpr double kDetrend = 60.0;
constexpr double kPeriodicMost = 0.5;

double outlineAt(double x, double z) {
    return fieldsAt(x, z).outline;
}

// The radius of the top of b along the direction a: where its outline changes sign, sought from
// guess, refined by bisection (0 if none).
double radiusAt(const Butte& b, double a, double guess) {
    const double dx = std::cos(a);
    const double dz = std::sin(a);
    const auto inside = [&](double r) { return outlineAt(b.x + dx * r, b.z + dz * r) >= 0.0; };
    double lo = std::max(0.0, guess);
    double hi = lo;
    if (inside(lo)) {
        for (hi = lo + kRadialStep; inside(hi); hi += kRadialStep) {
            if (hi > kRadiusMost) {
                return 0.0;
            }
            lo = hi;
        }
    } else {
        for (lo = hi - kRadialStep; lo > 0.0 && !inside(lo); lo -= kRadialStep) {
            hi = lo;
        }
        lo = std::max(lo, 0.0);
    }
    for (int k = 0; k < 12; ++k) {
        const double mid = 0.5 * (lo + hi);
        (inside(mid) ? lo : hi) = mid;
    }
    return 0.5 * (lo + hi);
}

// The radii of the top of b every degree (empty if lost).
std::vector<double> radiiOf(const Butte& b) {
    std::vector<double> radii;
    double guess = 0.0;
    for (int k = 0; k < kAngles; ++k) {
        const double r = radiusAt(b, 2.0 * kPi * k / kAngles, guess);
        if (!(r > 0.0)) {
            return {};
        }
        radii.push_back(r);
        guess = r;
    }
    return radii;
}

// The rms of the radii off their mean over kRoughSmooth meters of the rim.
double roughnessOf(const std::vector<double>& radii) {
    double mean = 0.0;
    for (const double r : radii) {
        mean += r / static_cast<double>(radii.size());
    }
    const auto half = static_cast<int>(0.5 * kRoughSmooth / (mean * 2.0 * kPi / kAngles));
    double squares = 0.0;
    for (int k = 0; k < kAngles; ++k) {
        double smooth = 0.0;
        for (int j = -half; j <= half; ++j) {
            smooth += radii[static_cast<std::size_t>((k + j + kAngles) % kAngles)];
        }
        smooth /= static_cast<double>(2 * half + 1);
        const double off = radii[static_cast<std::size_t>(k)] - smooth;
        squares += off * off;
    }
    return std::sqrt(squares / kAngles);
}

// What the rays of a butte show: the median of their batters (degrees), of their counts of ledges,
// of the depth of their first ledge (a share of the cliff), of their scree (meters rms), and the
// median horizontal run of their cliffs.
struct Walls {
    double batter = 0.0;
    double ledges = 0.0;
    double firstLedge = 0.0;
    double scree = 0.0;
    double run = 0.0;
    bool found = false;
};

double medianOf(std::vector<double> v) {
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// The ledges of a cliff ray: their count, the depth of the first (a share of the cliff, 0 if none).
std::array<double, 2> ledgesOf(const CliffRay& c) {
    const auto least = static_cast<std::size_t>(kLedgeLeast / c.step);
    const double fall = c.h[c.rim] - c.h[c.end];
    int count = 0;
    double first = 0.0;
    std::size_t run = 0;
    for (std::size_t i = c.rim + 1; i < c.end; ++i) {
        const bool flat = (c.h[i] - c.h[i + 1]) / c.step < kLedgeSlope;
        run = flat ? run + 1 : 0;
        if (run == least) {
            first = count == 0 ? (c.h[c.rim] - c.h[i]) / fall : first;
            ++count;
        }
    }
    return {static_cast<double>(count), first};
}

// The scree of a cliff ray: the rms of the talus within kScreeReach of its foot off its mean over
// kScreeSmooth.
double screeOf(const CliffRay& c) {
    const auto reach = static_cast<std::size_t>(kScreeReach / c.step);
    const auto half = static_cast<std::size_t>(0.5 * kScreeSmooth / c.step);
    double squares = 0.0;
    std::size_t count = 0;
    for (std::size_t i = c.end + half; i < c.end + reach && i + half < c.h.size(); ++i) {
        double mean = 0.0;
        for (std::size_t j = i - half; j <= i + half; ++j) {
            mean += c.h[j] / static_cast<double>(2 * half + 1);
        }
        squares += (c.h[i] - mean) * (c.h[i] - mean);
        ++count;
    }
    return count > 0 ? std::sqrt(squares / static_cast<double>(count)) : 0.0;
}

Walls wallsOf(const Butte& b) {
    std::vector<double> batters;
    std::vector<double> counts;
    std::vector<double> firsts;
    std::vector<double> screes;
    std::vector<double> runs;
    std::vector<CliffRay> rays;
    double tallest = 0.0;
    for (int k = 0; k < kRays; ++k) {
        const double a = 2.0 * kPi * (k + 0.5) / kRays;
        rays.push_back(cliffAlong(b.x, b.z, std::cos(a), std::sin(a), kRayStep));
        const CliffRay& c = rays.back();
        tallest = std::max(tallest, c.found ? c.h[c.rim] - c.h[c.end] : 0.0);
    }
    for (const CliffRay& c : rays) {
        const double fall = c.found ? c.h[c.rim] - c.h[c.end] : 0.0;
        if (!(fall >= kCliffLeast) || !(fall >= kWholeCliff * tallest)) {
            continue;
        }
        const double run = kRayStep * static_cast<double>(c.end - c.rim);
        batters.push_back(std::atan(fall / std::max(run, 1e-3)) * kDegrees);
        const std::array<double, 2> ledges = ledgesOf(c);
        counts.push_back(ledges[0]);
        if (ledges[0] > 0.0) {
            firsts.push_back(ledges[1]);
        }
        screes.push_back(screeOf(c));
        runs.push_back(run);
    }
    Walls w;
    w.found = batters.size() >= kRays / 2;
    w.batter = medianOf(batters);
    w.ledges = medianOf(counts);
    w.firstLedge = medianOf(firsts);
    w.scree = medianOf(screes);
    w.run = medianOf(runs);
    return w;
}

// The peak correlation of the heights along the foot of b (kFootOut meters out of its cliff), at the
// lags of kLagLow to kLagHigh meters.
double footPattern(const Butte& b, const std::vector<double>& radii, double run) {
    constexpr int kSamples = 2 * kAngles;
    std::vector<double> heights;
    double mean = 0.0;
    for (int k = 0; k < kSamples; ++k) {
        const double a = 2.0 * kPi * k / kSamples;
        const double r = radii[static_cast<std::size_t>(k / 2)] + run + kFootOut;
        heights.push_back(heightAt(b.x + std::cos(a) * r, b.z + std::sin(a) * r));
        mean += r / kSamples;
    }
    const double spacing = 2.0 * kPi * mean / kSamples;
    return pattern_test::peakCorrelation(
        pattern_test::detrended(heights, static_cast<std::size_t>(kDetrend / spacing)),
        static_cast<std::size_t>(kLagLow / spacing), static_cast<std::size_t>(kLagHigh / spacing));
}

double spreadOf(const std::vector<double>& v) {
    double mean = 0.0;
    for (const double x : v) {
        mean += x / static_cast<double>(v.size());
    }
    double squares = 0.0;
    for (const double x : v) {
        squares += (x - mean) * (x - mean) / static_cast<double>(v.size());
    }
    return std::sqrt(squares);
}

std::vector<Butte> caseButtes(const tablelands_fields::Sites& sites) {
    std::vector<Butte> out;
    for (std::size_t e = 0; e < sites.edges.size() && out.size() < kMostButtes; ++e) {
        for (const Butte& b : tablelands_buttes::buttesAround(sites.edges[e][0], sites.edges[e][1], 96)) {
            if (b.height >= kButteLow && b.height <= kButteHigh && b.wall > kSteep60 &&
                out.size() < kMostButtes) {
                out.push_back(b);
            }
        }
    }
    return out;
}

} // namespace

int main() {
    const tablelands_fields::Sites sites = tablelands_fields::findSites();
    const std::vector<Butte> buttes = caseButtes(sites);
    std::vector<double> roughs;
    std::vector<double> batters;
    std::vector<double> firsts;
    std::vector<double> screes;
    int ledged = 0;
    int walled = 0;
    double periodic = 0.0;
    for (const Butte& b : buttes) {
        const std::vector<double> radii = radiiOf(b);
        const Walls w = wallsOf(b);
        if (radii.empty() || !w.found) {
            continue;
        }
        const double rough = roughnessOf(radii);
        const double pattern = footPattern(b, radii, w.run);
        roughs.push_back(rough);
        batters.push_back(w.batter);
        screes.push_back(w.scree);
        if (w.firstLedge > 0.0) {
            firsts.push_back(w.firstLedge);
        }
        ledged += w.ledges >= 1.0 && w.ledges <= 3.0 ? 1 : 0;
        periodic = std::max(periodic, pattern);
        ++walled;
        std::printf("butte at (%.0f, %.0f), %.0f m: outline %.1f m rms, batter %.1f degrees, %.0f "
                    "ledges (the first %.2f down), scree %.1f m rms, foot periodic %.2f\n",
                    b.x, b.z, b.height, rough, w.batter, w.ledges, w.firstLedge, w.scree, pattern);
    }
    std::sort(batters.begin(), batters.end());
    const double batter = medianOf(batters);
    const double p10 = batters.empty() ? 0.0 : batters[batters.size() / 10];
    const double p90 = batters.empty() ? 0.0 : batters[batters.size() * 9 / 10];
    const auto steepest = std::count_if(batters.begin(), batters.end(),
                                        [](double v) { return v > kSteepest; });
    std::printf("cliffs: %d buttes; outline %.1f m rms (the median); batter %.1f degrees (10th "
                "percentile %.1f, 90th %.1f), %ld over %.0f degrees; ledges 1 to 3 on %d, the first "
                "%.2f down (spread %.2f); scree %.1f m rms; foot periodic %.2f at most\n",
                walled, medianOf(roughs), batter, p10, p90, static_cast<long>(steepest), kSteepest,
                ledged, medianOf(firsts), firsts.empty() ? 0.0 : spreadOf(firsts), medianOf(screes),
                periodic);
    TEST_CHECK(walled >= kLeastButtes);
    TEST_CHECK(medianOf(roughs) >= kLeastRough);
    TEST_CHECK(batter >= kBatterLow && batter <= kBatterHigh);
    TEST_CHECK(static_cast<double>(steepest) <= kSteepShare * static_cast<double>(walled));
    TEST_CHECK(p90 - p10 >= kLeastBatterSpread);
    TEST_CHECK(static_cast<double>(ledged) >= kLedgedShare * static_cast<double>(walled));
    TEST_CHECK(!firsts.empty() && spreadOf(firsts) >= kLeastLedgeSpread);
    TEST_CHECK(medianOf(screes) >= kLeastScree);
    TEST_CHECK(periodic < kPeriodicMost);
    return test::result();
}

// The slots of the falls in steps of the small world (phase 12A): they wind, their lips are never
// a straight cut across them, and nothing along them repeats at one spacing.
//   - Winding: on the corridors of the falls (the runs of points every few meters of a reach), the
//     axis moves aside from the chord of the corridor by kWindLeast of the half width of its slot
//     at least for the median corridor, at most by kWindMost of it, over bends whose half waves
//     vary (their coefficient of variation kWaveSpread at least).
//   - Lips: across the channel at kAcross of its half width on each side and on its axis, where
//     the ground comes down through the middle of the drop: over hard rock on soft (the contrast
//     of Heightfield::rockHardness kContrast at least), the middle of the lip lies upstream of its
//     sides by kRecessLeast of the drop at least on average (an amphitheater); over rock of one
//     hardness by kStraightMost meters at most (a straight lip), yet never as straight as a ruler
//     (its three places apart by kEdgeLeast meters at least on average).
//   - Banks: no more steps along them than kBankSteps per thousand samples (testBanks).
//   - Patterns: the walls of the slot along the corridors have no peak of autocorrelation above
//     kPeakMost at the lags of the noises of the slot (8 to 60 m).
#include "falls_support.hpp"
#include "pattern_support.hpp"
#include "terrain/streaming/lod.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

using falls::Reaches;
using falls::World;
using terrain::RiverReach;

constexpr double kRunGap = 20.0; // a corridor: its points closer than this
constexpr double kWindLeast = 0.18;
constexpr double kWindMost = 1.6;
constexpr double kWaveSpread = 0.3;
constexpr double kAcross = 0.8;
constexpr double kContrast = 0.3;
constexpr double kRecessLeast = 0.3;
constexpr double kStraightMost = 2.5;
constexpr double kEdgeLeast = 0.1;
constexpr double kPeakMost = 0.5;

// The corridors of a reach: their first and last points.
std::vector<std::array<std::size_t, 2>> corridorsOf(const RiverReach& reach) {
    std::vector<std::array<std::size_t, 2>> out;
    const auto& p = reach.points;
    std::size_t i = 0;
    while (i + 1 < p.size()) {
        std::size_t j = i;
        while (j + 1 < p.size() &&
               std::hypot(p[j + 1].x - p[j].x, p[j + 1].z - p[j].z) < kRunGap) {
            ++j;
        }
        if (j >= i + 4) {
            out.push_back({i, j});
        }
        i = j + 1;
    }
    return out;
}

// The half width of the slot at point i: the mean of its rims, or of the channel without them.
double halfWidth(const World& w, const RiverReach& reach, std::size_t i) {
    const std::array<double, 2> rims = falls::rimsAt(w, reach, i);
    const double channel = 0.5 * static_cast<double>(reach.points[i].width);
    return std::max(channel, 0.5 * (rims[0] + rims[1]));
}

// The signed distances aside of a path (every kStep meters along it) from its course at large
// (the path smoothed by a Gaussian of kSigma meters: the bends of the valley, not those of the
// slot), over its inside (kSigma from its ends).
constexpr double kStep = 2.0;
constexpr double kSigma = 60.0;
constexpr double kHysteresis = 1.0; // meters: a bend turns back by this much at least

std::vector<double> asideOf(const std::vector<std::array<double, 2>>& path) {
    std::vector<double> out;
    const auto reach = static_cast<std::ptrdiff_t>(3.0 * kSigma / kStep);
    const auto inner = static_cast<std::ptrdiff_t>(kSigma / kStep);
    const auto n = static_cast<std::ptrdiff_t>(path.size());
    for (std::ptrdiff_t i = inner; i + inner < n; ++i) {
        double x = 0.0;
        double z = 0.0;
        double weights = 0.0;
        for (std::ptrdiff_t k = std::max<std::ptrdiff_t>(0, i - reach);
             k <= std::min(n - 1, i + reach); ++k) {
            const double d = static_cast<double>(k - i) * kStep / kSigma;
            const double weight = std::exp(-0.5 * d * d);
            x += weight * path[static_cast<std::size_t>(k)][0];
            z += weight * path[static_cast<std::size_t>(k)][1];
            weights += weight;
        }
        const auto& a = path[static_cast<std::size_t>(std::max<std::ptrdiff_t>(0, i - 1))];
        const auto& b = path[static_cast<std::size_t>(std::min(n - 1, i + 1))];
        const double dx = b[0] - a[0];
        const double dz = b[1] - a[1];
        const double length = std::max(1e-9, std::hypot(dx, dz));
        const auto& p = path[static_cast<std::size_t>(i)];
        out.push_back(((p[0] - x / weights) * dz - (p[1] - z / weights) * dx) / length);
    }
    return out;
}

// The points of a reach from first to last every kStep meters along it.
std::vector<std::array<double, 2>> resampled(const RiverReach& reach, std::size_t first,
                                             std::size_t last) {
    std::vector<std::array<double, 2>> out;
    const auto& p = reach.points;
    double carry = 0.0;
    for (std::size_t i = first; i < last; ++i) {
        const double length = std::hypot(p[i + 1].x - p[i].x, p[i + 1].z - p[i].z);
        for (double t = carry; t < length; t += kStep) {
            const double f = t / length;
            out.push_back({p[i].x + f * (p[i + 1].x - p[i].x), p[i].z + f * (p[i + 1].z - p[i].z)});
            carry = t + kStep - length;
        }
    }
    return out;
}

void testWinding(const World& w, Reaches& reaches, const std::vector<std::size_t>& steeps) {
    std::vector<double> shares;
    std::vector<double> halves; // half waves, meters
    double most = 0.0;
    for (const std::size_t r : steeps) {
        const RiverReach& reach = reaches[r];
        for (const auto& [first, last] : corridorsOf(reach)) {
            const std::vector<double> aside = asideOf(resampled(reach, first, last));
            if (aside.size() < 20) {
                continue;
            }
            double far = 0.0;
            // The bends: where the axis turns back by kHysteresis from its farthest aside.
            double extremum = aside[0];
            std::size_t at = 0;
            int direction = 1;
            std::vector<std::size_t> bends;
            for (std::size_t i = 0; i < aside.size(); ++i) {
                far = std::max(far, std::abs(aside[i]));
                const double d = static_cast<double>(direction) * (aside[i] - extremum);
                if (d > 0.0) {
                    extremum = aside[i];
                    at = i;
                } else if (d < -kHysteresis) {
                    bends.push_back(at);
                    direction = -direction;
                    extremum = aside[i];
                    at = i;
                }
            }
            for (std::size_t k = 2; k < bends.size(); ++k) {
                halves.push_back(static_cast<double>(bends[k] - bends[k - 1]) * kStep);
            }
            const double half = halfWidth(w, reach, (first + last) / 2);
            shares.push_back(far / half);
            most = std::max(most, far / half);
        }
    }
    const double median = falls::quantile(shares, 0.5);
    const double spread = pattern_test::variation(halves);
    std::printf("%zu corridors of the falls: the axis aside from their course at large by %.2f of "
                "the half width of the slot (median, %.2f at most), %zu bends, %.0f m apart "
                "(median, %.0f to %.0f m from the 10th to the 90th percentile), varying by %.2f\n",
                shares.size(), median, most, halves.size(), falls::quantile(halves, 0.5),
                falls::quantile(halves, 0.1), falls::quantile(halves, 0.9), spread);
    TEST_CHECK(shares.size() >= 40);
    TEST_CHECK(median >= kWindLeast && most <= kWindMost);
    TEST_CHECK(spread >= kWaveSpread);
}

// How far upstream of lip i of a reach the water comes down through the middle of its drop,
// side meters left of the axis (searched from kSearchUp meters upstream every kSearchStep).
constexpr double kSearchUp = 30.0;
constexpr double kSearchStep = 0.1;

double edgeAt(const World& w, const RiverReach& reach, std::size_t i, double middle, double side) {
    const auto& p = reach.points;
    const double length = std::hypot(p[i + 1].x - p[i].x, p[i + 1].z - p[i].z);
    const double dx = (p[i + 1].x - p[i].x) / length;
    const double dz = (p[i + 1].z - p[i].z) / length;
    for (double t = -kSearchUp; t < 2.0 * length + 5.0; t += kSearchStep) {
        const double x = p[i].x + t * dx - side * dz;
        const double z = p[i].z + t * dz + side * dx;
        const terrain::HeightSample h = w.field.sampleAt(x, z);
        const double surface = h.water != terrain::WaterKind::None
                                   ? static_cast<double>(std::max(h.waterLevel, h.height))
                                   : static_cast<double>(h.height);
        if (surface < middle) {
            return -t;
        }
    }
    return std::numeric_limits<double>::quiet_NaN();
}

// The contrast of the rock under a lip at (x, z): the hardness of its rock under the water above
// less the softest under it (river_lips.cpp, as the generator reads it).
double contrastAt(const World& w, double x, double z, double top, double bottom) {
    const double cap = w.relief.rockHardness(x, z, top - 0.5);
    double under = 1.0;
    for (const double share : {0.5, 1.0, 1.5}) {
        under = std::min(under, static_cast<double>(
                                    w.relief.rockHardness(x, z, top - share * (top - bottom))));
    }
    return std::clamp(cap - under, 0.0, 1.0);
}

void testLips(const World& w, Reaches& reaches, const std::vector<std::size_t>& steeps) {
    std::vector<double> hollow;   // the middle upstream of the sides, by the drop (contrasted)
    std::vector<double> straight; // and meters (one hardness)
    std::vector<double> apart;    // the three places, the farthest apart (meters)
    for (const std::size_t r : steeps) {
        const RiverReach& reach = reaches[r];
        const auto& p = reach.points;
        for (std::size_t i = 1; i + 2 < p.size(); ++i) {
            const double run = std::hypot(p[i + 1].x - p[i].x, p[i + 1].z - p[i].z);
            if (!(p[i - 1].level == p[i].level && p[i].level - p[i + 1].level > 2.0f &&
                  static_cast<double>(p[i].level - p[i + 1].level) > run)) {
                continue;
            }
            std::size_t j = i + 1;
            while (j + 1 < p.size() && p[j].level - p[j + 1].level > 0.5f) {
                ++j;
            }
            const double top = p[i].level;
            const double bottom = p[j].level;
            const double half = kAcross * 0.5 * static_cast<double>(p[i].width);
            const double middle = 0.5 * (top + bottom);
            const double left = edgeAt(w, reach, i, middle, half);
            const double centre = edgeAt(w, reach, i, middle, 0.0);
            const double right = edgeAt(w, reach, i, middle, -half);
            if (std::isnan(left) || std::isnan(centre) || std::isnan(right)) {
                continue;
            }
            const double contrast = contrastAt(w, p[i].x, p[i].z, top, bottom);
            const double recess = centre - 0.5 * (left + right);
            if (contrast >= kContrast) {
                hollow.push_back(recess / (top - bottom));
            } else if (!(contrast > 0.0)) {
                straight.push_back(std::abs(recess));
                apart.push_back(std::max({left, centre, right}) - std::min({left, centre, right}));
            }
        }
    }
    double hollowMean = 0.0;
    for (const double v : hollow) {
        hollowMean += v / static_cast<double>(std::max<std::size_t>(1, hollow.size()));
    }
    double apartMean = 0.0;
    for (const double v : apart) {
        apartMean += v / static_cast<double>(std::max<std::size_t>(1, apart.size()));
    }
    const double straightMost = falls::quantile(straight, 0.95);
    std::printf("%zu lips over hard rock on soft: their middle %.2f of their drop upstream of "
                "their sides on average; %zu over rock of one hardness: %.2f m at most (95th "
                "percentile), their three places %.2f m apart on average\n",
                hollow.size(), hollowMean, straight.size(), straightMost, apartMean);
    TEST_CHECK(hollow.size() >= 5 && straight.size() >= 100);
    TEST_CHECK(hollowMean >= kRecessLeast);
    TEST_CHECK(straightMost <= kStraightMost && apartMean >= kEdgeLeast);
}

// The walls of the slot along the longest corridors, every kWallStep meters along them: on each
// side of the axis, the distance to where the ground first stands kWallRise meters over the water
// there (searched every kWallSearch meters, kWallFar at most), each side its own profile, less
// its mean over kWallWindow samples (30 m). No autocorrelation above kPeakMost at the lags of 8 to
// 60 m (the swings, edges, spurs and wander of the slot), on the profiles that vary at all.
constexpr std::size_t kWallCorridors = 12;
constexpr double kWallStep = 2.0;
constexpr double kWallRise = 1.0;
constexpr double kWallSearch = 0.5;
constexpr double kWallFar = 60.0;
constexpr std::size_t kWallWindow = 15;

// The profiles of the walls of the slot on each side along points first to last of a reach.
std::array<std::vector<double>, 2> wallsAlong(const World& w, const RiverReach& reach,
                                              std::size_t first, std::size_t last) {
    std::array<std::vector<double>, 2> out;
    double along = 0.0;
    double next = 0.0;
    for (std::size_t i = first; i < last; ++i) {
        const terrain::RiverPoint& a = reach.points[i];
        const terrain::RiverPoint& b = reach.points[i + 1];
        const double length = std::hypot(b.x - a.x, b.z - a.z);
        const double ux = -(b.z - a.z) / length;
        const double uz = (b.x - a.x) / length;
        for (; next < along + length; next += kWallStep) {
            const double t = (next - along) / length;
            const double x = a.x + t * (b.x - a.x);
            const double z = a.z + t * (b.z - a.z);
            const double water = a.level + t * (b.level - a.level);
            for (std::size_t side = 0; side < 2; ++side) {
                const double sign = side == 0 ? 1.0 : -1.0;
                double found = std::nan("");
                for (double d = kWallSearch; d <= kWallFar; d += kWallSearch) {
                    if (w.field.heightAt(x + sign * d * ux, z + sign * d * uz) >=
                        water + kWallRise) {
                        found = d;
                        break;
                    }
                }
                out[side].push_back(found);
            }
        }
        along += length;
    }
    return out;
}

void testPatterns(const World& w, Reaches& reaches, const std::vector<std::size_t>& steeps) {
    struct Corridor {
        std::size_t reach = 0;
        std::size_t first = 0;
        std::size_t last = 0;
        double length = 0.0;
    };
    std::vector<Corridor> corridors;
    for (const std::size_t r : steeps) {
        const RiverReach& reach = reaches[r];
        for (const auto& [first, last] : corridorsOf(reach)) {
            double length = 0.0;
            for (std::size_t i = first; i < last; ++i) {
                length += std::hypot(reach.points[i + 1].x - reach.points[i].x,
                                     reach.points[i + 1].z - reach.points[i].z);
            }
            corridors.push_back({r, first, last, length});
        }
    }
    std::sort(corridors.begin(), corridors.end(),
              [](const Corridor& a, const Corridor& b) { return a.length > b.length; });
    corridors.resize(std::min(corridors.size(), kWallCorridors));
    double peak = -1.0;
    std::size_t samples = 0;
    std::size_t profiles = 0;
    for (const Corridor& c : corridors) {
        for (const std::vector<double>& profile :
             wallsAlong(w, reaches[c.reach], c.first, c.last)) {
            samples += profile.size();
            const std::vector<double> rest = pattern_test::detrended(profile, kWallWindow);
            double spread = 0.0;
            for (const double v : rest) {
                spread = std::isnan(v) ? spread : std::max(spread, std::abs(v));
            }
            if (spread > 0.5) {
                ++profiles;
                peak = std::max(peak, pattern_test::peakCorrelation(rest, 4, 30));
            }
        }
    }
    std::printf("the walls of the slots of the %zu longest corridors, %zu places every %.0f m on "
                "%zu varying sides: autocorrelation %.2f at most at the lags of 8 to 60 m\n",
                corridors.size(), samples, kWallStep, profiles, peak);
    TEST_CHECK(samples >= 2000 && profiles >= 12);
    TEST_CHECK(peak <= kPeakMost);
}

// The blocks of the pools: in heaps at the foot of the drops (kHeapNear meters below the nearest
// foot of a drop upstream for the median block), of kBlockLeast to kBlockMost meters (their 90th
// percentile kBlockSpread times their 10th at least), a share of them kOutLeast to kOutMost out
// of the water of their pool, by kOutHigh meters at most.
constexpr double kHeapNear = 25.0;
constexpr double kBlockLeast = 1.5;
constexpr double kBlockMost = 7.0;
constexpr double kBlockSpread = 1.5;
constexpr double kOutLeast = 0.2;
constexpr double kOutMost = 0.6;
constexpr double kOutHigh = 4.0;

void testHeaps(Reaches& reaches, const std::vector<std::size_t>& steeps) {
    std::vector<double> widths;
    std::vector<double> below; // meters below the nearest foot upstream
    std::vector<double> out;   // meters out of the water, of those out of it
    for (const std::size_t r : steeps) {
        const RiverReach& reach = reaches[r];
        const auto& p = reach.points;
        std::vector<double> s(p.size(), 0.0);
        for (std::size_t i = 1; i < p.size(); ++i) {
            s[i] = s[i - 1] + std::hypot(p[i].x - p[i - 1].x, p[i].z - p[i - 1].z);
        }
        for (const terrain::RiverBlock& b : reach.blocks) {
            widths.push_back(2.0 * static_cast<double>(b.radius));
            std::size_t near = 0;
            for (std::size_t i = 1; i < p.size(); ++i) {
                if (std::hypot(p[i].x - b.x, p[i].z - b.z) <
                    std::hypot(p[near].x - b.x, p[near].z - b.z)) {
                    near = i;
                }
            }
            std::size_t foot = near;
            while (foot > 0 && !(p[foot - 1].level - p[foot].level > 2.0f)) {
                --foot;
            }
            below.push_back(s[near] - s[foot]);
            const double water = p[near].level;
            if (static_cast<double>(b.top) > water) {
                out.push_back(static_cast<double>(b.top) - water);
            }
        }
    }
    const double share = static_cast<double>(out.size()) /
                         static_cast<double>(std::max<std::size_t>(1, widths.size()));
    const double least = falls::quantile(widths, 0.0);
    const double most = falls::quantile(widths, 1.0);
    const double spread =
        falls::quantile(widths, 0.9) / std::max(1e-9, falls::quantile(widths, 0.1));
    const double median = falls::quantile(below, 0.5);
    const double highest = falls::quantile(out, 1.0);
    std::printf("%zu blocks: %.1f to %.1f m wide (their 90th percentile %.1f times their 10th), "
                "%.0f m below the foot of a drop (median), %.0f%% out of the water, by %.1f m "
                "(median) to %.1f m\n",
                widths.size(), least, most, spread, median, 100.0 * share,
                falls::quantile(out, 0.5), highest);
    TEST_CHECK(widths.size() >= 100);
    TEST_CHECK(least >= kBlockLeast - 1e-3 && most <= kBlockMost + 1e-3 && spread >= kBlockSpread);
    TEST_CHECK(median <= kHeapNear);
    TEST_CHECK(share >= kOutLeast && share <= kOutMost && highest <= kOutHigh + 1e-3);
}

// The banks of the slot along the corridors: on lines alongside the axis, kBankOut meters beyond
// the channel on each side, every 0.5 m, the ground steps by more than kBankStep meters at most
// kBankSteps times per thousand samples (a slot winding down a steep valley would step its banks
// where the segments of its axis turn: river_falls.cpp).
constexpr std::array<double, 2> kBankOut = {6.0, 15.0};
constexpr double kBankStep = 2.0;
constexpr double kBankSteps = 0.25;

void testBanks(const World& w, Reaches& reaches, const std::vector<std::size_t>& steeps) {
    std::size_t samples = 0;
    std::size_t steps = 0;
    for (const std::size_t r : steeps) {
        const auto& p = reaches[r].points;
        for (std::size_t i = 0; i + 1 < p.size(); ++i) {
            const double length = std::hypot(p[i + 1].x - p[i].x, p[i + 1].z - p[i].z);
            if (!(length < kRunGap) || !(length > 0.0)) {
                continue;
            }
            const double ux = (p[i + 1].x - p[i].x) / length;
            const double uz = (p[i + 1].z - p[i].z) / length;
            for (const double side : {-1.0, 1.0}) {
                for (const double out : kBankOut) {
                    const double d = side * (0.5 * static_cast<double>(p[i].width) + out);
                    double before = std::nan("");
                    for (double t = 0.0; t <= length; t += 0.5) {
                        const double h = w.field.heightAt(p[i].x + t * ux - d * uz,
                                                          p[i].z + t * uz + d * ux);
                        if (!std::isnan(before)) {
                            ++samples;
                            steps += std::abs(h - before) > kBankStep ? 1 : 0;
                        }
                        before = h;
                    }
                }
            }
        }
    }
    const double perThousand = 1000.0 * static_cast<double>(steps) /
                               static_cast<double>(std::max<std::size_t>(1, samples));
    std::printf("the banks of the corridors: %zu samples, %zu steps of more than %.0f m (%.2f per "
                "thousand)\n",
                samples, steps, kBankStep, perThousand);
    TEST_CHECK(samples >= 100'000 && perThousand <= kBankSteps);
}

} // namespace

int main() {
    const World w = falls::makeWorld();
    Reaches reaches(*w.hydrology);
    const std::vector<std::size_t> steeps = falls::steepReaches(w, reaches);
    testWinding(w, reaches, steeps);
    testLips(w, reaches, steeps);
    testPatterns(w, reaches, steeps);
    testHeaps(reaches, steeps);
    testBanks(w, reaches, steeps);
    return test::result();
}

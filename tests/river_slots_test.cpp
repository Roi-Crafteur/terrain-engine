// The slots of the falls in steps of the small world (phase 12A): they wind, as far aside as the
// width of their channel about, whatever the slope; their banks never step but where their water
// falls; no levee raises the ground beside them; nothing along their walls repeats at one spacing.
//   - Winding: on the corridors of the falls (the points of a reach every few meters where its
//     slot winds aside of the axis of its course), the middle of the water found on the ground
//     across the axis lies aside of it by kWindLeast to kWindMost times the width of the channel
//     at most (along each corridor long enough for a wave, kLongWidths widths: every other
//     point, the median of three neighbors) in kWindShare of them at least, kWindMedian for the
//     median one; over bends whose half waves vary (their coefficient of variation kWaveSpread
//     at least).
//   - Bends: the slot runs at kAngleMost degrees to the axis at most, and bends no tighter than
//     kRadiusLeast times the half width of its channel (its 1st percentile). At both ends of every
//     reach it lies on the axis: whatever lies beyond (the sea, a lake, another river), the slot
//     meets it there.
//   - Banks: no more steps along them than kBankSteps per thousand samples, kAwaySteps but where
//     the water of the slot falls (a face crosses a bank where the slot runs at an angle to it).
//   - Rims: down the stretches in steps, the ground beside the slot stands kRaise meters above the
//     relief at kRaisedShare of their points at most, kRaiseMost at most (no levee there).
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
using terrain::RiverPoint;
using terrain::RiverReach;

constexpr double kRunGap = 20.0; // a corridor: its points closer than this
constexpr double kWindLeast = 0.5;
constexpr double kWindMost = 1.5;
constexpr double kWindShare = 0.85;
constexpr double kWindMedian = 0.7;
constexpr double kLongWidths = 10.0;
constexpr double kWaveSpread = 0.3;
constexpr double kAngleMost = 42.0;
constexpr double kRadiusLeast = 0.75;
constexpr double kRaise = 0.5;
constexpr double kRaisedShare = 0.05;
constexpr double kRaiseMost = 2.0;
constexpr double kPeakMost = 0.5;

// The corridors of a reach: their first and last points (a run of points closer than kRunGap,
// some of them aside of the axis).
std::vector<std::array<std::size_t, 2>> corridorsOf(const RiverReach& reach) {
    std::vector<std::array<std::size_t, 2>> out;
    const auto& p = reach.points;
    std::size_t i = 0;
    while (i + 1 < p.size()) {
        std::size_t j = i;
        bool wound = false;
        while (j + 1 < p.size() &&
               std::hypot(p[j + 1].x - p[j].x, p[j + 1].z - p[j].z) < kRunGap) {
            wound = wound || p[j].shift != 0.0f;
            ++j;
        }
        if (j >= i + 4 && wound) {
            out.push_back({i, j});
        }
        i = j + 1;
    }
    return out;
}

// The middle of the water across the axis at point k of a reach (the wet run nearest to where the
// slot winds, found every kAcrossStep meters), meters left of the axis, or NaN.
constexpr double kAcrossStep = 2.0;

double waterMiddle(const World& w, const RiverReach& reach, std::size_t k) {
    const auto& p = reach.points;
    const double dx = p[k + 1].axisX - p[k - 1].axisX;
    const double dz = p[k + 1].axisZ - p[k - 1].axisZ;
    const double length = std::hypot(dx, dz);
    if (!(length > 0.0)) {
        return std::nan("");
    }
    const double reachAcross = 2.0 * static_cast<double>(p[k].width) + std::abs(p[k].shift);
    double best = std::nan("");
    double start = std::nan("");
    for (double d = -reachAcross; d <= reachAcross + kAcrossStep; d += kAcrossStep) {
        const double x = p[k].axisX - d * dz / length;
        const double z = p[k].axisZ + d * dx / length;
        const bool wet =
            d <= reachAcross && w.field.sampleAt(x, z).water != terrain::WaterKind::None;
        if (wet && std::isnan(start)) {
            start = d;
        } else if (!wet && !std::isnan(start)) {
            const double middle = 0.5 * (start + d - kAcrossStep);
            if (std::isnan(best) || std::abs(middle - p[k].shift) < std::abs(best - p[k].shift)) {
                best = middle;
            }
            start = std::nan("");
        }
    }
    return best;
}

// The half waves of the shift of the slot along a corridor (meters along the axis between its
// turns back by kHysteresis meters at least from its farthest aside).
constexpr double kHysteresis = 1.0;

void halfWaves(const RiverReach& reach, std::size_t first, std::size_t last,
               std::vector<double>& out) {
    const auto& p = reach.points;
    double along = 0.0;
    double extremum = p[first].shift;
    double at = 0.0;
    int direction = 1;
    std::vector<double> bends;
    for (std::size_t i = first + 1; i <= last; ++i) {
        along += std::hypot(p[i].axisX - p[i - 1].axisX, p[i].axisZ - p[i - 1].axisZ);
        const double d = static_cast<double>(direction) * (p[i].shift - extremum);
        if (d > 0.0) {
            extremum = p[i].shift;
            at = along;
        } else if (d < -kHysteresis) {
            bends.push_back(at);
            direction = -direction;
            extremum = p[i].shift;
            at = along;
        }
    }
    for (std::size_t k = 2; k < bends.size(); ++k) {
        out.push_back(bends[k] - bends[k - 1]);
    }
}

void testWinding(const World& w, Reaches& reaches, const std::vector<std::size_t>& steeps) {
    std::vector<double> shares;
    std::vector<double> halves;
    std::size_t within = 0;
    for (const std::size_t r : steeps) {
        const RiverReach& reach = reaches[r];
        const auto& p = reach.points;
        for (const auto& [first, last] : corridorsOf(reach)) {
            double length = 0.0;
            for (std::size_t k = first; k < last; ++k) {
                length += std::hypot(p[k + 1].axisX - p[k].axisX, p[k + 1].axisZ - p[k].axisZ);
            }
            const double width = p[(first + last) / 2].width;
            if (length < kLongWidths * width) {
                continue;
            }
            std::vector<double> aside;
            for (std::size_t k = first + 1; k < last; k += 2) {
                const double middle = waterMiddle(w, reach, k);
                if (!std::isnan(middle)) {
                    aside.push_back(std::abs(middle));
                }
            }
            double far = 0.0;
            for (std::size_t k = 1; k + 1 < aside.size(); ++k) {
                far = std::max(far, std::max(std::min(aside[k - 1], aside[k]),
                                             std::min(std::max(aside[k - 1], aside[k]),
                                                      aside[k + 1])));
            }
            const double share = far / width;
            shares.push_back(share);
            within += share >= kWindLeast && share <= kWindMost ? 1 : 0;
            halfWaves(reach, first, last, halves);
        }
    }
    const double inside = static_cast<double>(within) / static_cast<double>(shares.size());
    const double median = falls::quantile(shares, 0.5);
    const double spread = pattern_test::variation(halves);
    std::printf("%zu corridors of the falls long enough for a wave: the water aside of the axis by "
                "%.2f of the width of the channel at most (median, %.2f to %.2f "
                "from the 10th to the 90th), %.0f%% of them within %.1f to %.1f; %zu half waves, "
                "%.0f m (median, %.0f to %.0f m), varying by %.2f\n",
                shares.size(), median, falls::quantile(shares, 0.1), falls::quantile(shares, 0.9),
                100.0 * inside, kWindLeast, kWindMost, halves.size(),
                falls::quantile(halves, 0.5), falls::quantile(halves, 0.1),
                falls::quantile(halves, 0.9), spread);
    TEST_CHECK(shares.size() >= 40);
    TEST_CHECK(inside >= kWindShare && median >= kWindMedian);
    TEST_CHECK(spread >= kWaveSpread);
}

// The angle of the slot to the axis at each point of the corridors, and the radius of its bends
// (through three points 4 to kRunGap meters apart), in half widths of its channel.
void testBends(Reaches& reaches, const std::vector<std::size_t>& steeps) {
    double steepest = 0.0;
    std::vector<double> radii;
    std::size_t ends = 0;
    for (const std::size_t r : steeps) {
        const auto& p = reaches[r].points;
        ends += (p.front().shift != 0.0f ? 1 : 0) + (p.back().shift != 0.0f ? 1 : 0);
        for (std::size_t i = 1; i + 1 < p.size(); ++i) {
            const double a = std::hypot(p[i].x - p[i - 1].x, p[i].z - p[i - 1].z);
            const double b = std::hypot(p[i + 1].x - p[i].x, p[i + 1].z - p[i].z);
            if (p[i].shift == 0.0f || a < 4.0 || b < 4.0 || a > kRunGap || b > kRunGap) {
                continue;
            }
            const double ax = p[i + 1].axisX - p[i - 1].axisX;
            const double az = p[i + 1].axisZ - p[i - 1].axisZ;
            const double sx = p[i + 1].x - p[i - 1].x;
            const double sz = p[i + 1].z - p[i - 1].z;
            const double turn = std::abs(std::atan2(ax * sz - az * sx, ax * sx + az * sz));
            steepest = std::max(steepest, turn * 180.0 / 3.14159265358979323846);
            const double c = std::hypot(sx, sz);
            const double area = std::abs((p[i].x - p[i - 1].x) * sz - (p[i].z - p[i - 1].z) * sx);
            radii.push_back(area > 0.0 ? a * b * c / (2.0 * area) / (0.5 * p[i].width) : 1e9);
        }
    }
    const double tightest = falls::quantile(radii, 0.01);
    std::printf("the slots wind at %.1f degrees to the axis at most, their bends %.2f half widths "
                "of their channel round at least (1st percentile of %zu); %zu ends of %zu reaches "
                "aside of their axis\n",
                steepest, tightest, radii.size(), ends, steeps.size());
    TEST_CHECK(radii.size() >= 1000 && ends == 0);
    TEST_CHECK(steepest <= kAngleMost && tightest >= kRadiusLeast);
}

// The banks of the slot along the corridors: on lines alongside the slot, kBankOut meters beyond
// the channel on each side, every 0.5 m, the ground steps by more than kBankStep meters at most
// kBankSteps times per thousand samples, and never farther than kFaceNear meters plus the
// distance aside times kFaceAngle along the water from where it falls (a face crosses the bank
// there where the slot runs at an angle to the line of its lip).
constexpr std::array<double, 2> kBankOut = {6.0, 15.0};
constexpr double kBankStep = 2.0;
constexpr double kBankSteps = 0.25;
constexpr double kAwaySteps = 0.02;
constexpr double kFaceNear = 25.0;
constexpr double kFaceAngle = 0.85;

void testBanks(const World& w, Reaches& reaches, const std::vector<std::size_t>& steeps) {
    std::size_t samples = 0;
    std::size_t steps = 0;
    std::size_t away = 0;
    for (const std::size_t r : steeps) {
        const auto& p = reaches[r].points;
        std::vector<double> s(p.size(), 0.0);
        std::vector<double> falls;
        for (std::size_t i = 1; i < p.size(); ++i) {
            s[i] = s[i - 1] + std::hypot(p[i].x - p[i - 1].x, p[i].z - p[i - 1].z);
            if (p[i - 1].level - p[i].level > 0.5f) {
                falls.push_back(0.5 * (s[i - 1] + s[i]));
            }
        }
        for (std::size_t i = 0; i + 1 < p.size(); ++i) {
            const double length = s[i + 1] - s[i];
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
                            if (std::abs(h - before) > kBankStep) {
                                ++steps;
                                const double near = kFaceNear + kFaceAngle * std::abs(d);
                                away += std::none_of(falls.begin(), falls.end(), [&](double f) {
                                    return std::abs(f - (s[i] + t)) < near;
                                }) ? 1 : 0;
                            }
                        }
                        before = h;
                    }
                }
            }
        }
    }
    const double perThousand = 1000.0 * static_cast<double>(steps) /
                               static_cast<double>(std::max<std::size_t>(1, samples));
    const double awayPerThousand = 1000.0 * static_cast<double>(away) /
                                   static_cast<double>(std::max<std::size_t>(1, samples));
    std::printf("the banks of the corridors: %zu samples, %zu steps of more than %.0f m (%.2f per "
                "thousand), %zu of them away from the falls of their water (%.3f per thousand)\n",
                samples, steps, kBankStep, perThousand, away, awayPerThousand);
    TEST_CHECK(samples >= 100'000 && perThousand <= kBankSteps);
    TEST_CHECK(awayPerThousand <= kAwaySteps);
}

// The ground beside the slots down the stretches in steps (their points where the floor of the
// valley lies more than a meter above the water: its slot), from 5 m beyond the channel to 80 m
// from its middle, every 2 m on each side, against the relief.
void testRims(const World& w, Reaches& reaches, const std::vector<std::size_t>& steeps) {
    std::size_t points = 0;
    std::size_t raised = 0;
    double most = 0.0;
    for (const std::size_t r : steeps) {
        const auto& p = reaches[r].points;
        for (std::size_t i = 0; i + 1 < p.size(); i += 2) {
            const double dx = p[i + 1].x - p[i].x;
            const double dz = p[i + 1].z - p[i].z;
            const double length = std::hypot(dx, dz);
            if (!(p[i].valley - p[i].level > 1.0f) || !(length > 0.0) || !(length < kRunGap)) {
                continue;
            }
            double rise = 0.0;
            for (const double side : {-1.0, 1.0}) {
                for (double d = 0.5 * static_cast<double>(p[i].width) + 5.0; d <= 80.0; d += 2.0) {
                    const double x = p[i].x - side * d * dz / length;
                    const double z = p[i].z + side * d * dx / length;
                    rise = std::max(rise, static_cast<double>(w.field.heightAt(x, z) -
                                                              w.relief.heightAt(x, z)));
                }
            }
            ++points;
            raised += rise > kRaise ? 1 : 0;
            most = std::max(most, rise);
        }
    }
    const double share =
        static_cast<double>(raised) / static_cast<double>(std::max<std::size_t>(1, points));
    std::printf("beside the slots of %zu points of stretches in steps: the ground more than %.1f m "
                "over the relief at %.1f%% of them, %.2f m at most\n",
                points, kRaise, 100.0 * share, most);
    TEST_CHECK(points >= 500);
    TEST_CHECK(share <= kRaisedShare && most <= kRaiseMost);
}

// The walls of the slot along the longest corridors, every kWallStep meters along them: on each
// side of the slot, the distance to where the ground first stands kWallRise meters over the water
// there (searched every kWallCoarse meters, then every kWallSearch meters over the last of them,
// kWallFar at most), each side its own profile, less
// its mean over kWallWindow samples (30 m). No autocorrelation above kPeakMost at the lags of 8 to
// 60 m (the swings, edges, spurs and wander of the slot), on the profiles that vary at all.
constexpr std::size_t kWallCorridors = 12;
constexpr double kWallStep = 2.0;
constexpr double kWallRise = 1.0;
constexpr double kWallSearch = 0.5;
constexpr double kWallCoarse = 2.0;
constexpr double kWallFar = 60.0;
constexpr std::size_t kWallWindow = 15;

std::array<std::vector<double>, 2> wallsAlong(const World& w, const RiverReach& reach,
                                              std::size_t first, std::size_t last) {
    std::array<std::vector<double>, 2> out;
    double along = 0.0;
    double next = 0.0;
    for (std::size_t i = first; i < last; ++i) {
        const RiverPoint& a = reach.points[i];
        const RiverPoint& b = reach.points[i + 1];
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
                const auto above = [&](double d) {
                    return w.field.heightAt(x + sign * d * ux, z + sign * d * uz) >=
                           water + kWallRise;
                };
                double found = std::nan("");
                for (double d = kWallCoarse; d <= kWallFar; d += kWallCoarse) {
                    if (above(d)) {
                        found = d;
                        for (double e = d - kWallCoarse + kWallSearch; e < d; e += kWallSearch) {
                            if (above(e)) {
                                found = e;
                                break;
                            }
                        }
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

} // namespace

int main() {
    const World w = falls::makeWorld();
    Reaches reaches(*w.hydrology);
    const std::vector<std::size_t> steeps = falls::steepReaches(w, reaches);
    testWinding(w, reaches, steeps);
    testBends(reaches, steeps);
    testBanks(w, reaches, steeps);
    testRims(w, reaches, steeps);
    testPatterns(w, reaches, steeps);
    return test::result();
}

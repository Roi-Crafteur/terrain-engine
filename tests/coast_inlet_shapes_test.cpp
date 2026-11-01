// The shapes of the inlets and stacks (agent 33, commit 4 bis, after the check in Unreal of the
// stop point 2), on the default world:
// - The fjords: wide valleys in U between massifs. Across their axis at their site: water at least
//   kFjordWater wide, the valley at least kFjordValley wide kFjordRelief meters up for most of
//   them, both sides rising kFjordRelief meters within kFjordReach meters of the water for most of
//   them; their flanks steep but never a slit: over 100 m, their slope at the 90th percentile at
//   most kFjordSteepest, and in the middle of the fjords at most kFjordMedian.
// - The coves: on the coasts that rise (never a low coast at their mouth, the ground kCoveTop
//   meters up within kCoveReach meters of their site), opening out to the sea (their mouth at least
//   kCoveMouth of their widest water inland, for kCoveOpen of them at least).
// - The stacks: domes of rock out at sea, kStackLeast meters high at least; their flank (a fifth
//   to four fifths of the way out) at least as steep as 1 and of rock.
#include "terrain/generation/coast.hpp"
#include "terrain/generation/ecology.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/surface_material.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <numbers>
#include <vector>

namespace {

using terrain::CoastKind;
using terrain::CoastSite;
using terrain::InletKind;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

const terrain::TerrainTiles& tiles() {
    static const terrain::TerrainTiles t(kSeed);
    return t;
}

const terrain::Heightfield& field() {
    return tiles().heightfield();
}

double signalAt(double x, double z) {
    return field().coastAt(x, z).signal;
}

double heightAt(double x, double z) {
    return static_cast<double>(field().heightAt(x, z));
}

// The value of v at the share p of its sorted values.
double quantile(std::vector<double> v, double p) {
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    const auto k = static_cast<std::size_t>(p * static_cast<double>(v.size()));
    return v[std::min(v.size() - 1, k)];
}

// Fjords: kFjords of them, the first by x then z 300 km around the coasts of the fjords.
constexpr std::array<std::array<double, 2>, 2> kFjordCoasts{
    {{3'624'471.0, -10'006'796.0}, {1'793'548.0, 9'821'452.0}}};
constexpr std::size_t kFjords = 24;
constexpr double kFjordWater = 1'000.0;
constexpr double kFjordValley = 2'000.0;
constexpr double kFjordRelief = 300.0;
constexpr double kFjordReach = 3'000.0;
constexpr double kFjordSteepest = 2.2;
constexpr double kFjordMedian = 1.2;
// The share of the fjords whose valley and both sides pass kFjordValley and kFjordRelief.
constexpr double kFjordMost = 0.6;

// How far the water runs from (x, z) toward angle t, radians (6 km at most).
double waterRun(double x, double z, double t) {
    double d = 0.0;
    while (d < 6'000.0 &&
           signalAt(x + std::cos(t) * (d + 25.0), z + std::sin(t) * (d + 25.0)) < 0.0) {
        d += 25.0;
    }
    return d;
}

// A side of the cross-section of a fjord: its shore, where it reaches kFjordRelief meters, its
// highest within kFjordReach meters of the water and the 90th percentile of its slope over 100 m
// from the water to there.
struct Bank {
    double shore = 0.0;
    double up = 1e9;
    double highest = -1e9;
    double steep = 0.0;
};

Bank bankOf(double x, double z, double nx, double nz) {
    Bank b;
    while (b.shore < 4'000.0 && signalAt(x + nx * b.shore, z + nz * b.shore) < 0.0) {
        b.shore += 10.0;
    }
    double at = b.shore;
    for (double d = b.shore; d <= b.shore + kFjordReach; d += 25.0) {
        const double h = heightAt(x + nx * d, z + nz * d);
        b.up = h >= kFjordRelief ? std::min(b.up, d) : b.up;
        if (h > b.highest) {
            b.highest = h;
            at = d;
        }
    }
    std::vector<double> slopes;
    for (double d = b.shore; d + 100.0 <= std::max(at, b.shore + 200.0); d += 25.0) {
        slopes.push_back(std::abs(heightAt(x + nx * (d + 100.0), z + nz * (d + 100.0)) -
                                  heightAt(x + nx * d, z + nz * d)) /
                         100.0);
    }
    b.steep = quantile(slopes, 0.9);
    return b;
}

void testFjords() {
    std::vector<CoastSite> fjords;
    for (const auto& [cx, cz] : kFjordCoasts) {
        for (const CoastSite& s : field().inletsIn(cx - 3e5, cz - 3e5, cx + 3e5, cz + 3e5)) {
            if (s.inlet == InletKind::Fjord && fjords.size() < kFjords) {
                fjords.push_back(s);
            }
        }
    }
    TEST_CHECK(fjords.size() == kFjords);
    std::vector<double> steep;
    int wide = 0;
    int most = 0;
    for (const CoastSite& s : fjords) {
        // The axis: the direction of the longest water through the site.
        double best = -1.0;
        double axis = 0.0;
        for (int a = 0; a < 36; ++a) {
            const double t = a * std::numbers::pi / 36.0;
            const double run = waterRun(s.x, s.z, t) + waterRun(s.x, s.z, t + std::numbers::pi);
            if (run > best) {
                best = run;
                axis = t;
            }
        }
        const Bank l = bankOf(s.x, s.z, -std::sin(axis), std::cos(axis));
        const Bank r = bankOf(s.x, s.z, std::sin(axis), -std::cos(axis));
        const double valley = l.up + r.up;
        const double low = std::min(l.highest, r.highest);
        const double worst = std::max(l.steep, r.steep);
        std::printf("fjord (%.0f, %.0f): water %.0f m, valley %.0f m at %.0f m, sides up to "
                    "%.0f / %.0f m, slope p90 %.2f / %.2f\n",
                    s.x, s.z, l.shore + r.shore, valley < 1e8 ? valley : -1.0, kFjordRelief,
                    l.highest, r.highest, l.steep, r.steep);
        wide += l.shore + r.shore >= kFjordWater ? 1 : 0;
        most += valley >= kFjordValley && valley < 1e8 && low >= kFjordRelief ? 1 : 0;
        steep.push_back(worst);
    }
    const double n = static_cast<double>(fjords.size());
    std::printf("%zu fjords: %d with water %.0f m wide at least, %d between sides %.0f m high "
                "(valley %.0f m wide at least); steeper side, slope p90: median %.2f, most %.2f\n",
                fjords.size(), wide, kFjordWater, most, kFjordRelief, kFjordValley,
                quantile(steep, 0.5), quantile(steep, 1.0));
    TEST_CHECK(wide == static_cast<int>(fjords.size()));
    TEST_CHECK(static_cast<double>(most) >= kFjordMost * n);
    TEST_CHECK(quantile(steep, 0.5) <= kFjordMedian);
    TEST_CHECK(quantile(steep, 1.0) <= kFjordSteepest);
}

// Coves and stacks: near the coasts of rocky shores and cliffs of the tests of the stop points.
constexpr std::array<std::array<double, 2>, 3> kShoreCoasts{{{23'433'101.0, -2'128'828.0},
                                                              {-15'720'496.0, -6'266'783.0},
                                                              {-26'486'937.0, 7'219'391.0}}};
constexpr std::size_t kCoves = 60;
constexpr double kCoveTop = 8.0;
constexpr double kCoveReach = 500.0;
constexpr double kCoveMouth = 0.75;
constexpr double kCoveOpen = 0.8;

void testCoves() {
    int found = 0;
    int low = 0;
    int flat = 0;
    int open = 0;
    for (const auto& [cx, cz] : kShoreCoasts) {
        for (const CoastSite& s : field().inletsIn(cx - 5e4, cz - 5e4, cx + 5e4, cz + 5e4)) {
            if (s.inlet != InletKind::Cove || found >= static_cast<int>(kCoves)) {
                continue;
            }
            ++found;
            low += s.coast == CoastKind::Low ? 1 : 0;
            double top = -1e9;
            for (int a = 0; a < 16; ++a) {
                const double t = a * std::numbers::pi / 8.0;
                for (const double r : {0.5 * kCoveReach, 0.75 * kCoveReach, kCoveReach}) {
                    top = std::max(top, heightAt(s.x + r * std::cos(t), s.z + r * std::sin(t)));
                }
            }
            flat += top < kCoveTop ? 1 : 0;
            if (top < kCoveTop || s.opening < kCoveMouth * s.size) {
                std::printf("  cove (%.0f, %.0f) at a %s coast: opening %.0f m, widest %.0f m, "
                            "ground up to %.1f m within %.0f m\n",
                            s.x, s.z, terrain::coastKindName(s.coast), s.opening, s.size, top,
                            kCoveReach);
            }
            open += s.opening >= kCoveMouth * s.size ? 1 : 0;
        }
    }
    std::printf("%d coves: %d at a low coast, %d on ground under %.0f m within %.0f m, %d with "
                "a mouth %.0f %% of their widest at least\n",
                found, low, flat, kCoveTop, kCoveReach, open, 100.0 * kCoveMouth);
    TEST_CHECK(found == static_cast<int>(kCoves));
    TEST_CHECK(low == 0);
    TEST_CHECK(flat == 0);
    TEST_CHECK(static_cast<double>(open) >= kCoveOpen * found);
}

constexpr std::size_t kStacks = 30;
constexpr double kStackLeast = 30.0;
constexpr double kStackSteep = 1.0;
constexpr double kStackRock = 0.7;

// The material of the ecology at (x, z), its slope read over 1 m (the LOD 0 of the tiles).
terrain::SurfaceMaterial materialAt(double x, double z) {
    const terrain::HeightSample s = field().sampleAt(x, z);
    terrain::SurfaceSite site;
    site.ground = s.height;
    site.water = s.water;
    site.waterLevel = s.water == terrain::WaterKind::None ? s.height : s.waterLevel;
    site.gradX = static_cast<float>(0.5 * (heightAt(x + 1.0, z) - heightAt(x - 1.0, z)));
    site.gradZ = static_cast<float>(0.5 * (heightAt(x, z + 1.0) - heightAt(x, z - 1.0)));
    site.steepness = std::hypot(site.gradX, site.gradZ);
    site.continentalness = s.continentalness;
    return tiles().ecology().sampleAt(x, z, site).material;
}

void testStacks() {
    std::vector<terrain::CoastStack> stacks;
    for (const auto& [cx, cz] : kShoreCoasts) {
        for (const terrain::CoastStack& s :
             field().stacksIn(cx - 5e4, cz - 5e4, cx + 5e4, cz + 5e4)) {
            if (stacks.size() < kStacks) {
                stacks.push_back(s);
            }
        }
    }
    TEST_CHECK(stacks.size() == kStacks);
    int high = 0;
    int flank = 0;
    int steep = 0;
    int rock = 0;
    for (const terrain::CoastStack& s : stacks) {
        high += heightAt(s.x, s.z) >= kStackLeast ? 1 : 0;
        for (int a = 0; a < 12; ++a) {
            const double t = a * std::numbers::pi / 6.0;
            for (const double u : {0.2, 0.35, 0.5, 0.65, 0.8}) {
                const double r = u * static_cast<double>(s.radius);
                const double x = s.x + r * std::cos(t);
                const double z = s.z + r * std::sin(t);
                const double slope = std::hypot(heightAt(x + 1.0, z) - heightAt(x - 1.0, z),
                                                heightAt(x, z + 1.0) - heightAt(x, z - 1.0)) /
                                     2.0;
                ++flank;
                steep += slope >= kStackSteep ? 1 : 0;
                rock += materialAt(x, z) == terrain::SurfaceMaterial::Rock ? 1 : 0;
            }
        }
    }
    std::printf("%zu stacks: %d at least %.0f m high; their flanks: %d points, %d at least %.1f "
                "steep, %d of rock\n",
                stacks.size(), high, kStackLeast, flank, steep, kStackSteep, rock);
    TEST_CHECK(high == static_cast<int>(stacks.size()));
    TEST_CHECK(static_cast<double>(steep) >= kStackRock * flank);
    TEST_CHECK(static_cast<double>(rock) >= kStackRock * flank);
}

} // namespace

int main() {
    testFjords();
    testCoves();
    testStacks();
    return test::result();
}

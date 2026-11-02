// The faces of the rock of the slots of the falls and of the gorges (phase 12A) on the small world:
// - the faces of the slots (water/slot_faces.hpp), not drawn by the tiles yet (kSlotFacesDrawn,
//   rock_bands_test): their notch deeper and higher in softer rock, and varied along long straight
//   walls without repeating itself (the detector of rock_patterns_test);
// - the walls of the gorges (heightfield_hydrology.cpp, gorgeFaces): carved faces only on the
//   risers of their strata 3 m high or more (as those of the mountains), few (the sections across
//   their deep stretches, gorge kGorgeDeep, that have some are counted), none across the open
//   valleys (gorge below kGorgeOpen) kFarFromFalls meters from any fall.
#include "falls_support.hpp"
#include "pattern_support.hpp"
#include "slot_faces.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
#include <utility>
#include <vector>

namespace {

using terrain::RockColumn;
using terrain::RockFace;

constexpr double kWallLength = 2000.0; // the long straight walls of testNoPeriod, 1 m steps
constexpr double kDetrend = 40.0;      // the swells slower than this left out (rock_patterns)
constexpr double kLeastLag = 8.0;      // the spacings checked: beyond the width of a notch
constexpr double kMostLag = 80.0;
constexpr double kIrregular = 0.5;
constexpr double kLeastVariation = 0.03;
constexpr float kGorgeDeep = 0.7f;
constexpr float kGorgeOpen = 0.3f;
constexpr double kFarFromFalls = 300.0;
constexpr double kGorgeHalf = 120.0; // the sections across the gorges, every 2 m
constexpr std::size_t kGorgeEvery = 6; // points of a reach between two of them
constexpr std::size_t kOpenEvery = 40; // the same across the open valleys

const RockFace* carvedOf(const RockColumn& c) {
    for (std::size_t i = 0; i < c.faceCount; ++i) {
        if (c.faces[i].carved) {
            return &c.faces[i];
        }
    }
    return nullptr;
}

// slotFace along long straight walls of one hardness: its notch varies (kLeastVariation of its
// depth at least) without repeating itself (the detector of rock_patterns_test), whatever the
// direction of the wall.
void testNoPeriod() {
    terrain::SlotWall w;
    w.width = 8.0f;
    w.foot = 100.0f;
    w.top = 130.0f;
    w.water = 100.0f;
    w.airX = 1.0f;
    w.weight = 1.0f;
    double peak = 0.0;
    double least = 1e9;
    for (const double angle : {0.0, 0.7, 1.9, 2.6}) {
        std::vector<double> depths;
        std::vector<double> heights;
        for (double t = 0.0; t < kWallLength; t += 1.0) {
            const RockFace f = terrain::water::slotFace(w, 1000.0 + t * std::cos(angle),
                                                        -3000.0 + t * std::sin(angle), 0.5f,
                                                        golden::kSmallPlanet);
            depths.push_back(f.undercut);
            heights.push_back(f.undercutHeight);
        }
        for (const std::vector<double>* p : {&depths, &heights}) {
            peak = std::max(peak, pattern_test::peakCorrelation(
                                      pattern_test::detrended(*p,
                                                              static_cast<std::size_t>(kDetrend)),
                                      static_cast<std::size_t>(kLeastLag),
                                      static_cast<std::size_t>(kMostLag)));
        }
        least = std::min(least, pattern_test::variation(depths));
    }
    std::printf("the notches along straight walls of %.0f m: variation %.3f at least, peak %.2f\n",
                kWallLength, least, peak);
    TEST_CHECK(least >= kLeastVariation && peak < kIrregular);
}

// slotFace: a notch deeper and higher in soft rock than in hard rock, at the same place.
void testHardness() {
    terrain::SlotWall w;
    w.width = 8.0f;
    w.foot = 100.0f;
    w.top = 130.0f;
    w.water = 100.0f;
    w.airX = 1.0f;
    w.weight = 1.0f;
    std::size_t deeper = 0;
    std::size_t places = 0;
    for (double x = 0.0; x < 400.0; x += 7.3) {
        const RockFace soft = terrain::water::slotFace(w, x, 0.5 * x, 0.0f, golden::kSmallPlanet);
        const RockFace hard = terrain::water::slotFace(w, x, 0.5 * x, 1.0f, golden::kSmallPlanet);
        ++places;
        deeper += soft.undercut > hard.undercut && soft.undercutHeight >= hard.undercutHeight;
    }
    TEST_CHECK(deeper == places);
}

// Sections across the valley of reach at its points of gorge in [low, high], one every every
// points, kFarFromFalls meters from its falls: those with carved faces, and all of them.
std::pair<std::size_t, std::size_t> gorgeSections(const falls::World& w,
                                                  const terrain::RiverReach& reach, float low,
                                                  float high, std::size_t every) {
    std::size_t faced = 0;
    std::size_t all = 0;
    for (std::size_t i = 1; i + 1 < reach.points.size(); i += every) {
        const terrain::RiverPoint& p = reach.points[i];
        const terrain::RiverPoint& q = reach.points[i + 1];
        const bool far = std::all_of(reach.falls.begin(), reach.falls.end(), [&](const auto& f) {
            return std::hypot(f.x - p.x, f.z - p.z) > kFarFromFalls;
        });
        const double length = std::hypot(q.x - p.x, q.z - p.z);
        if (p.gorge < low || p.gorge > high || !far || !(length > 0.0)) {
            continue;
        }
        const double nx = -(q.z - p.z) / length;
        const double nz = (q.x - p.x) / length;
        std::vector<double> xs;
        std::vector<double> zs;
        for (double t = -kGorgeHalf; t <= kGorgeHalf; t += 2.0) {
            xs.push_back(p.x + nx * t);
            zs.push_back(p.z + nz * t);
        }
        std::vector<terrain::HeightSample> out(xs.size());
        std::vector<RockColumn> rock(xs.size());
        w.field.samplesAt(xs, zs, out, rock);
        ++all;
        faced += std::any_of(rock.begin(), rock.end(),
                             [](const RockColumn& c) { return carvedOf(c) != nullptr; });
    }
    return {faced, all};
}

// The courses of the reaches falls::steepReaches looks at, computed on every core first (the
// hydrology keeps them): most of the time of the test.
void warmCourses(const falls::World& w) {
    std::vector<std::size_t> candidates;
    for (const auto& segment : w.planet->rivers().segments) {
        const auto& v = segment.vertices;
        for (std::size_t k = 0; k + 1 < v.size(); ++k) {
            if (v[k].elevation - v[k + 1].elevation >= falls::kSteepGraph) {
                for (const std::size_t r : w.hydrology->reachesNear(v[k].x, v[k].z, 1.0)) {
                    candidates.push_back(r);
                }
            }
        }
    }
    std::atomic<std::size_t> next{0};
    std::vector<std::jthread> pool;
    const unsigned threads = std::max(1U, std::thread::hardware_concurrency());
    for (unsigned t = 0; t < threads; ++t) {
        pool.emplace_back([&] {
            for (std::size_t i = next++; i < candidates.size(); i = next++) {
                static_cast<void>(w.hydrology->reach(candidates[i]));
            }
        });
    }
}

} // namespace

int main() {
    const falls::World w = falls::makeWorld();
    warmCourses(w);
    falls::Reaches reaches(*w.hydrology);
    std::size_t gorgeFaced = 0;
    std::size_t gorgeAll = 0;
    std::size_t openFaced = 0;
    std::size_t openAll = 0;
    for (const std::size_t r : falls::steepReaches(w, reaches)) {
        const auto [gf, ga] = gorgeSections(w, reaches[r], kGorgeDeep, 1.0f, kGorgeEvery);
        const auto [of, oa] = gorgeSections(w, reaches[r], 0.0f, kGorgeOpen, kOpenEvery);
        gorgeFaced += gf;
        gorgeAll += ga;
        openFaced += of;
        openAll += oa;
    }
    std::printf("gorges: %zu of the %zu sections across deep ones with carved faces, %zu of the %zu "
                "across open valleys\n",
                gorgeFaced, gorgeAll, openFaced, openAll);
    TEST_CHECK(gorgeAll >= 20 && openAll >= 20 && openFaced == 0);
    testNoPeriod();
    testHardness();
    return test::result();
}

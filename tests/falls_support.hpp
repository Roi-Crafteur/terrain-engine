#pragma once

// What the tests of the falls in steps share (river_falls_test, river_slots_test): the small world
// with its water, its reaches built once, those of its falls, and measures of their slots.
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include "terrain/streaming/lod.hpp"

#include "golden_support.hpp"
#include "planet_golden_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <memory>
#include <thread>
#include <vector>

namespace falls {

using terrain::Heightfield;
using terrain::Hydrology;
using terrain::RiverReach;

struct World {
    Heightfield relief;
    std::shared_ptr<const terrain::PlanetLevel> planet;
    std::shared_ptr<const Hydrology> hydrology;
    Heightfield field;
};

inline World makeWorld() {
    const Heightfield relief(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                             golden::kSmallPlanet);
    const int threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    auto planet = std::make_shared<const terrain::PlanetLevel>(
        terrain::planet::buildPlanet(relief, golden::smallPlanetParams(threads)),
        golden::smallPlanetParams(threads));
    auto hydrology =
        std::make_shared<const Hydrology>(terrain::kDefaultWorldSeed, relief, planet);
    return {relief, planet, hydrology, relief.withHydrology(hydrology)};
}

// The courses of the reaches, each built once.
class Reaches {
public:
    explicit Reaches(const Hydrology& h) : h_(h) {}
    const RiverReach& operator[](std::size_t r) {
        auto it = built_.find(r);
        if (it == built_.end()) {
            it = built_.emplace(r, h_.reach(r)).first;
        }
        return it->second;
    }

private:
    const Hydrology& h_;
    std::map<std::size_t, RiverReach> built_;
};

// A reach whose water comes down more than kSteep meters per meter between two of its points:
// on a stretch in steps, or where it would be.
constexpr double kSteep = 0.12;
constexpr double kCorridor = 20'000.0;

inline bool steep(const RiverReach& reach) {
    for (std::size_t i = 1; i < reach.points.size(); ++i) {
        const terrain::RiverPoint& a = reach.points[i - 1];
        const terrain::RiverPoint& b = reach.points[i];
        const double run = std::hypot(b.x - a.x, b.z - a.z);
        if (run > 0.0 && static_cast<double>(a.level - b.level) > kSteep * run) {
            return true;
        }
    }
    return false;
}

// The reaches of the segments of the graph whose water comes down kSteepGraph meters at least
// between two points: those of the falls, mostly.
constexpr float kSteepGraph = 50.0f;

inline std::vector<std::size_t> steepReaches(const World& w, Reaches& reaches) {
    std::vector<std::size_t> out;
    const auto& segments = w.planet->rivers().segments;
    for (std::size_t s = 0; s < segments.size(); ++s) {
        const auto& v = segments[s].vertices;
        for (std::size_t k = 0; k + 1 < v.size(); ++k) {
            if (!(v[k].elevation - v[k + 1].elevation >= kSteepGraph)) {
                continue;
            }
            for (const std::size_t r : w.hydrology->reachesNear(v[k].x, v[k].z, 1.0)) {
                const RiverReach& reach = reaches[r];
                if (reach.segment == static_cast<std::int32_t>(s) &&
                    reach.vertex == static_cast<std::int32_t>(k) && steep(reach)) {
                    out.push_back(r);
                }
            }
        }
    }
    return out;
}

// The depth of the slot at point i of a reach: the floor of its valley (kFloorRise above the
// level it is carved from) over its water.
constexpr double kFloorRise = 0.6;

inline double depthAt(const RiverReach& reach, std::size_t i) {
    const terrain::RiverPoint& p = reach.points[i];
    return static_cast<double>(p.valley) + kFloorRise - static_cast<double>(p.level);
}

// The rim of the slot at point i of a reach on each side: the first distance from its axis where
// the ground comes within kRimBelow meters of the floor (0 when it never does within kRimFar).
constexpr double kRimBelow = 0.3;
constexpr double kRimFar = 80.0;
constexpr double kRimStep = 0.25;

inline std::array<double, 2> rimsAt(const World& w, const RiverReach& reach, std::size_t i) {
    const terrain::RiverPoint& p = reach.points[i];
    const terrain::RiverPoint& a = reach.points[i > 0 ? i - 1 : i];
    const terrain::RiverPoint& b = reach.points[std::min(i + 1, reach.points.size() - 1)];
    const double length = std::max(1e-6, std::hypot(b.x - a.x, b.z - a.z));
    const double ux = -(b.z - a.z) / length;
    const double uz = (b.x - a.x) / length;
    const double floor = static_cast<double>(p.valley) + kFloorRise - kRimBelow;
    std::array<double, 2> rims{0.0, 0.0};
    for (int side = 0; side < 2; ++side) {
        const double sign = side == 0 ? 1.0 : -1.0;
        for (double d = kRimStep; d <= kRimFar; d += kRimStep) {
            if (static_cast<double>(w.field.heightAt(p.x + sign * d * ux, p.z + sign * d * uz)) >=
                floor) {
                rims[static_cast<std::size_t>(side)] = d;
                break;
            }
        }
    }
    return rims;
}

// The p-th quantile of values (sorted in place).
inline double quantile(std::vector<double>& values, double p) {
    std::sort(values.begin(), values.end());
    if (values.empty()) {
        return 0.0;
    }
    return values[static_cast<std::size_t>(p * static_cast<double>(values.size() - 1))];
}

// The ends of the corridors of the falls (river_falls_test): the runs of points of a reach closer
// than kRunGap, sampled kEndSpan meters around their ends every kEndStep meters.
constexpr double kRunGap = 20.0;
constexpr double kEndSpan = 40.0;
constexpr double kEndStep = 0.5;
constexpr double kWaterJump = 0.2;
constexpr double kBedJump = 0.2;
constexpr double kNearDrop = 5.0;
constexpr double kNeighbors = 150.0;

struct RunEnd {
    std::size_t reach = 0;
    std::size_t point = 0;
    std::size_t run = 0; // the n-th run found
    double x = 0.0;
    double z = 0.0;
};

// The ends of the runs of a reach: their points.
inline void endsOf(const RiverReach& reach, std::size_t r, std::size_t& runs,
                   std::vector<RunEnd>& out) {
    const auto& p = reach.points;
    std::size_t i = 0;
    while (i + 1 < p.size()) {
        std::size_t j = i;
        while (j + 1 < p.size() && std::hypot(p[j + 1].x - p[j].x, p[j + 1].z - p[j].z) < kRunGap) {
            ++j;
        }
        if (j >= i + 2) {
            out.push_back({r, i, runs, p[i].x, p[i].z});
            out.push_back({r, j, runs, p[j].x, p[j].z});
            ++runs;
        }
        i = j + 1;
    }
}

// The greatest jumps of the water and of the ground along the axis of a reach r around point at,
// within its valley and off its blocks, those within kNearDrop of a drop of its water left out,
// and those on the
// faces of its falls (the ground coming down kFaceDrop meters within kFaceRun of the sample: a
// lip may recede upstream of where the water drops along the axis).
constexpr double kFaceDrop = 2.0;
constexpr double kFaceRun = 3.0;

struct Jumps {
    double water = 0.0;
    double bed = 0.0;
};

struct AxisSample {
    double along = 0.0;
    terrain::HeightSample h;
    bool ours = false;
};

inline Jumps jumpsAround(const World& w, const RiverReach& reach, std::size_t r, std::size_t at) {
    const auto& p = reach.points;
    std::vector<double> s(p.size(), 0.0);
    for (std::size_t i = 1; i < p.size(); ++i) {
        s[i] = s[i - 1] + std::hypot(p[i].x - p[i - 1].x, p[i].z - p[i - 1].z);
    }
    std::vector<AxisSample> samples;
    for (double along = std::max(0.0, s[at] - kEndSpan);
         along <= std::min(s.back(), s[at] + kEndSpan); along += kEndStep) {
        std::size_t i = 0;
        while (i + 2 < p.size() && s[i + 1] < along) {
            ++i;
        }
        const double t =
            s[i + 1] > s[i] ? std::clamp((along - s[i]) / (s[i + 1] - s[i]), 0.0, 1.0) : 0.0;
        const double x = p[i].x + t * (p[i + 1].x - p[i].x);
        const double z = p[i].z + t * (p[i + 1].z - p[i].z);
        // Within the valley of this river only (another one may cross the axis there).
        const terrain::RiverCarve carve = w.hydrology->riversAt(
            terrain::wrapCoordinate(x, golden::kSmallPlanet.x),
            terrain::wrapCoordinate(z, golden::kSmallPlanet.z), w.relief.heightAt(x, z));
        // Off the blocks of its pools (domes over the bed).
        const bool block = std::any_of(reach.blocks.begin(), reach.blocks.end(),
                                       [&](const terrain::RiverBlock& b) {
                                           return std::hypot(b.x - x, b.z - z) < b.radius + 0.5;
                                       });
        samples.push_back({along, w.field.sampleAt(x, z), carve.reach == r && !block});
    }
    const auto onFace = [&](std::size_t k) {
        const auto run = static_cast<std::size_t>(kFaceRun / kEndStep);
        const std::size_t a = k > run ? k - run : 0;
        const std::size_t b = std::min(samples.size() - 1, k + run);
        for (std::size_t i = a; i < b; ++i) {
            for (std::size_t j = i + 1; j <= b; ++j) {
                if (samples[i].h.height - samples[j].h.height > kFaceDrop) {
                    return true;
                }
            }
        }
        for (std::size_t i = 1; i < p.size(); ++i) {
            if (p[i - 1].level - p[i].level > 1.0f && samples[k].along > s[i - 1] - kNearDrop &&
                samples[k].along < s[i] + kNearDrop) {
                return true;
            }
        }
        return false;
    };
    Jumps out;
    for (std::size_t k = 1; k < samples.size(); ++k) {
        const terrain::HeightSample& a = samples[k - 1].h;
        const terrain::HeightSample& b = samples[k].h;
        if (!samples[k - 1].ours || !samples[k].ours || onFace(k)) {
            continue;
        }
        out.bed = std::max(out.bed, std::abs(static_cast<double>(b.height - a.height)));
        if (a.water != terrain::WaterKind::None && b.water != terrain::WaterKind::None) {
            out.water =
                std::max(out.water, std::abs(static_cast<double>(b.waterLevel - a.waterLevel)));
        }
    }
    return out;
}

} // namespace falls

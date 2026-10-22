#include "terrain/io/relief_stats.hpp"

#include "../generation/planet/planet_parallel.hpp"
#include "relief_stats_support.hpp"
#include "terrain/det_math.hpp"
#include "terrain/io/relief_view.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace terrain {

namespace {

using relief_support::percentile;

constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr double kDegrees = 360.0 / kTwoPi;
constexpr double kTan45 = 1.0;
constexpr double kTan70 = 2.7474774194546222;
// Slopes: 8 x 8 points kSlopeStep apart around the center of a cell, over 2 m.
constexpr int kSlopeSide = 8;
constexpr double kSlopeStep = 250.0;
// Summits: the highest of 25 x 25 points 500 m apart, climbed in steps of 100, 20 and 4 m.
constexpr int kSearchHalf = 12;
constexpr double kSearchStep = 500.0;
// The ring of the dominance, the drop of the faces along 16 lines in steps of kFaceStep.
constexpr double kRingLow = 3000.0;
constexpr double kRingHigh = 5000.0;
constexpr double kFaceDrop = 1500.0;
constexpr double kFaceStep = 25.0;
constexpr double kFaceReach = 6000.0;
// The floors: 8 to 14 km away; the ground within kOwnTop of the summit is its own top.
constexpr std::array<double, 4> kFloors = {8'000.0, 10'000.0, 12'000.0, 14'000.0};
constexpr double kOwnTop = 300.0;
constexpr double kEye = 1.8;
constexpr double kFloorRun = 25.0;

struct Peak {
    double x = 0.0;
    double z = 0.0;
    float height = 0.0f;
};

struct CoreFigures {
    double cliffs = 0.0;
    double walls = 0.0;
    double slopes = 0.0;
    Peak summit;
    double dominance = 0.0;
    double faces = -1.0; // degrees; negative when no line drops kFaceDrop
    double seen = -1.0;  // degrees; negative when no floor sees the summit
    double floorSlope = 0.0;
};

void addSlopes(const Heightfield& field, double cx, double cz, CoreFigures& c) {
    for (int j = 0; j < kSlopeSide; ++j) {
        for (int i = 0; i < kSlopeSide; ++i) {
            const double x = cx + (i - 0.5 * (kSlopeSide - 1)) * kSlopeStep;
            const double z = cz + (j - 0.5 * (kSlopeSide - 1)) * kSlopeStep;
            const double gx = 0.5 * static_cast<double>(field.heightAt(x + 1.0, z) -
                                                        field.heightAt(x - 1.0, z));
            const double gz = 0.5 * static_cast<double>(field.heightAt(x, z + 1.0) -
                                                        field.heightAt(x, z - 1.0));
            const double slope = dmath::hypot(gx, gz);
            c.cliffs += slope > kTan45 ? 1.0 : 0.0;
            c.walls += slope > kTan70 ? 1.0 : 0.0;
            c.slopes += 1.0;
        }
    }
}

Peak highestAround(const Heightfield& field, const Peak& from, int half, double step) {
    Peak best = from;
    for (int j = -half; j <= half; ++j) {
        for (int i = -half; i <= half; ++i) {
            const double x = from.x + i * step;
            const double z = from.z + j * step;
            const float h = field.heightAt(x, z);
            best = h > best.height ? Peak{x, z, h} : best;
        }
    }
    return best;
}

Peak summitNear(const Heightfield& field, double x, double z) {
    Peak best = highestAround(field, {x, z, field.heightAt(x, z)}, kSearchHalf, kSearchStep);
    for (const double step : {100.0, 20.0, 4.0}) {
        for (int k = 0; k < 64; ++k) {
            const Peak next = highestAround(field, best, 1, step);
            if (!(next.height > best.height)) {
                break;
            }
            best = next;
        }
    }
    return best;
}

double along(const Peak& p, double distance, double turns, bool z) {
    return z ? p.z - distance * dmath::cosTurns(turns) : p.x + distance * dmath::sinTurns(turns);
}

// The dominance of the summit over its ring and the mean slope of its faces.
void measureSummit(const Heightfield& field, CoreFigures& c) {
    const Peak& s = c.summit;
    float ring = -1.0e30f;
    for (double r = kRingLow; r <= kRingHigh; r += 250.0) {
        for (int k = 0; k < 64; ++k) {
            ring = std::max(ring, field.heightAt(along(s, r, k / 64.0, false),
                                                 along(s, r, k / 64.0, true)));
        }
    }
    c.dominance = static_cast<double>(s.height - ring);
    double run = 0.0;
    int lines = 0;
    for (int k = 0; k < 16; ++k) {
        for (double d = kFaceStep; d <= kFaceReach; d += kFaceStep) {
            const float h = field.heightAt(along(s, d, k / 16.0, false), along(s, d, k / 16.0, true));
            if (static_cast<double>(h) <= static_cast<double>(s.height) - kFaceDrop) {
                run += d;
                ++lines;
                break;
            }
        }
    }
    c.faces = lines > 0 ? dmath::atan(kFaceDrop * lines / run) * kDegrees : -1.0;
}

// The lowest floor that sees the summit: the candidates from the lowest up, until one sees it.
void measureFloor(const Heightfield& field, CoreFigures& c) {
    const ViewGround ground = [&field](double x, double z) { return field.heightAt(x, z); };
    const Peak& s = c.summit;
    std::vector<Peak> floors;
    for (const double d : kFloors) {
        for (int k = 0; k < 64; ++k) {
            const Peak p{along(s, d, k / 64.0, false), along(s, d, k / 64.0, true), 0.0f};
            const float h = field.heightAt(p.x, p.z);
            if (h >= 1.0f) {
                floors.push_back({p.x, p.z, h});
            }
        }
    }
    std::stable_sort(floors.begin(), floors.end(),
                     [](const Peak& a, const Peak& b) { return a.height < b.height; });
    for (const Peak& f : floors) {
        ViewParams eye;
        eye.eyeX = f.x;
        eye.eyeZ = f.z;
        eye.eyeHeight = static_cast<double>(f.height) + kEye;
        const Sightline line =
            sightline(ground, eye, s.x, s.z, static_cast<double>(s.height), kOwnTop);
        if (line.visible) {
            c.seen = dmath::atan(line.tangent) * kDegrees;
            const double gx = static_cast<double>(field.heightAt(f.x + kFloorRun, f.z) -
                                                  field.heightAt(f.x - kFloorRun, f.z));
            const double gz = static_cast<double>(field.heightAt(f.x, f.z + kFloorRun) -
                                                  field.heightAt(f.x, f.z - kFloorRun));
            c.floorSlope = dmath::atan(dmath::hypot(gx, gz) / (2.0 * kFloorRun)) * kDegrees;
            return;
        }
    }
}

double shareAtLeast(const std::vector<double>& values, double bound) {
    const auto n = std::count_if(values.begin(), values.end(),
                                 [bound](double v) { return v >= bound; });
    return values.empty() ? 0.0 : static_cast<double>(n) / static_cast<double>(values.size());
}

} // namespace

PeakFigures measurePeaks(const Heightfield& field, const ReliefGrid& world,
                         const ReliefStats& stats, int count, int threads) {
    PeakFigures f;
    if (field.geography() == nullptr) {
        return f;
    }
    const int workers = planet::resolveThreads(threads);
    const std::vector<std::size_t> belts =
        relief_support::spread(relief_support::beltCells(field, world, stats), 8 * count);
    std::vector<std::uint8_t> core(belts.size(), 0);
    planet::parallelRows(static_cast<int>(belts.size()), workers, [&](int begin, int end) {
        for (int k = begin; k < end; ++k) {
            const std::size_t cell = belts[static_cast<std::size_t>(k)];
            const int i = static_cast<int>(cell % static_cast<std::size_t>(world.columns));
            const int j = static_cast<int>(cell / static_cast<std::size_t>(world.columns));
            core[static_cast<std::size_t>(k)] =
                field.sampleAt(world.x(i), world.z(j)).relief >= kCoreRelief ? 1 : 0;
        }
    });
    std::vector<std::size_t> cores;
    for (std::size_t k = 0; k < belts.size(); ++k) {
        if (core[k] != 0) {
            cores.push_back(belts[k]);
        }
    }
    cores = relief_support::spread(cores, count);
    std::vector<CoreFigures> figures(cores.size());
    planet::parallelRows(static_cast<int>(cores.size()), workers, [&](int begin, int end) {
        for (int k = begin; k < end; ++k) {
            const std::size_t cell = cores[static_cast<std::size_t>(k)];
            const double x = world.x(static_cast<int>(cell % static_cast<std::size_t>(world.columns)));
            const double z = world.z(static_cast<int>(cell / static_cast<std::size_t>(world.columns)));
            CoreFigures& c = figures[static_cast<std::size_t>(k)];
            addSlopes(field, x, z, c);
            c.summit = summitNear(field, x, z);
            measureSummit(field, c);
            measureFloor(field, c);
        }
    });
    double cliffs = 0.0;
    double walls = 0.0;
    double slopes = 0.0;
    std::vector<double> dominance;
    std::vector<double> faces;
    std::vector<double> seen;
    std::vector<double> floors;
    for (const CoreFigures& c : figures) {
        cliffs += c.cliffs;
        walls += c.walls;
        slopes += c.slopes;
        dominance.push_back(c.dominance);
        if (c.faces >= 0.0) {
            faces.push_back(c.faces);
        }
        if (c.seen >= 0.0) {
            seen.push_back(c.seen);
            floors.push_back(c.floorSlope);
        }
        f.highest = std::max(f.highest, static_cast<double>(c.summit.height));
    }
    f.cores = static_cast<int>(figures.size());
    f.cliffShare = slopes > 0.0 ? cliffs / slopes : 0.0;
    f.wallShare = slopes > 0.0 ? walls / slopes : 0.0;
    f.dominance50 = percentile(dominance, 0.5);
    f.dominantShare = shareAtLeast(dominance, kDominant);
    f.faces50 = percentile(faces, 0.5);
    f.steepFaces = shareAtLeast(faces, kSteepFaces);
    f.seen50 = percentile(seen, 0.5);
    f.seenShare = shareAtLeast(seen, kSeenAngle);
    f.floorSlope50 = percentile(floors, 0.5);
    return f;
}

} // namespace terrain

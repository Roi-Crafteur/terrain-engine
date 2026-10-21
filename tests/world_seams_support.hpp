#pragma once

// Helpers of the tests of the seams of the wrapped world (WorldSize): points anywhere and next to
// the edges, on a lattice of 1/64 m so that their images are exact, the images of a point, the
// rule of the grids, and walks of 1 cm steps across an edge, the step across it compared with the
// steps on both sides.

#include "terrain/generation/noise.hpp"
#include "terrain/world_size.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

namespace seams_test {

inline constexpr terrain::WorldSize kWorld{};

struct Point {
    double x;
    double z;
};

// Deterministic points: a third anywhere, a third within 64 m of the edge x = world.x / 2, a third
// within 64 m of the corner (world.x / 2, world.z / 2), on both sides; multiples of 1/64 m.
inline std::vector<Point> testPoints(int count, std::uint64_t stream) {
    const auto unit = [stream](std::uint64_t index) {
        return static_cast<double>(terrain::noise::deriveSeed(stream, index) >> 40) / 16777216.0;
    };
    const auto lattice = [](double v) { return std::floor(v * 64.0) / 64.0; };
    std::vector<Point> points;
    for (int n = 0; n < count; ++n) {
        const auto i = static_cast<std::uint64_t>(n);
        const double u = unit(2 * i) - 0.5;
        const double v = unit(2 * i + 1) - 0.5;
        if (n % 3 == 0) {
            points.push_back({lattice(u * kWorld.x), lattice(v * kWorld.z)});
        } else if (n % 3 == 1) {
            points.push_back({lattice(kWorld.x / 2.0 + 128.0 * u), lattice(v * kWorld.z)});
        } else {
            points.push_back(
                {lattice(kWorld.x / 2.0 + 128.0 * u), lattice(kWorld.z / 2.0 + 128.0 * v)});
        }
    }
    return points;
}

// Images of a point: moved by (k, l) whole worlds, all exact for the points above.
inline constexpr std::array<std::array<int, 2>, 4> kImages = {{{1, 0}, {0, -1}, {-2, 3}, {1, 1}}};

inline Point imageOf(Point p, const std::array<int, 2>& k) {
    return {p.x + k[0] * kWorld.x, p.z + k[1] * kWorld.z};
}

// The rule of every grid of the world: round(period / nominal) cells (at least one), of the
// size period / count.
inline bool followsTheRule(const terrain::PeriodicCells& cells, double period, double nominal) {
    const double count = std::max(1.0, std::round(period / nominal));
    return static_cast<double>(cells.count) == count && cells.size == period / count &&
           cells.period == period;
}

// A walk of 1 cm steps across an edge (or the corner), along (dx, dz), from up to 2 m before it to
// as far after it: x = world.x / 2 at a mid-latitude, z = world.z / 2 (the poles), the corner.
struct Walk {
    Point edge;
    double dx;
    double dz;
    const char* name;
};

inline const std::array<Walk, 3> kWalks = {
    {{{kWorld.x / 2.0, -10282000.0}, 1.0, 0.0, "x edge"},
     {{1234567.0, kWorld.z / 2.0}, 0.0, 1.0, "z edge"},
     {{kWorld.x / 2.0, kWorld.z / 2.0}, 1.0, 1.0, "corner"}}};

// True if no component of values (a function of (x, z) giving a fixed number of floats) jumps
// across the edge of the walk: the step across it stays within twice the largest step of the
// steps on either side, plus 4 units in the last place of the values. Prints the largest ratio.
template <typename Values>
bool continuousAcross(const Walk& walk, Values values, const char* name, int steps = 200) {
    const int kSteps = steps;
    std::vector<std::vector<float>> samples;
    for (int j = -kSteps; j <= kSteps; ++j) {
        samples.push_back(
            values(walk.edge.x + 0.01 * j * walk.dx, walk.edge.z + 0.01 * j * walk.dz));
    }
    bool continuous = true;
    double worst = 0.0;
    for (std::size_t c = 0; c < samples.front().size(); ++c) {
        double elsewhere = 0.0;
        double across = 0.0;
        float largest = 0.0f;
        for (std::size_t k = 1; k < samples.size(); ++k) {
            const double step = std::abs(static_cast<double>(samples[k][c]) - samples[k - 1][c]);
            if (k == static_cast<std::size_t>(kSteps)) {
                across = step;
            } else {
                elsewhere = std::max(elsewhere, step);
            }
            largest = std::max(largest, std::abs(samples[k][c]));
        }
        const double ulp = static_cast<double>(
            std::nextafter(largest, std::numeric_limits<float>::infinity()) - largest);
        continuous = continuous && across <= 2.0 * elsewhere + 4.0 * ulp;
        worst = std::max(worst, across / std::max(elsewhere, 4.0 * ulp));
    }
    std::printf("  %s across the %s: step across / largest step beside, at most %.2f\n", name,
                walk.name, worst);
    return continuous;
}

} // namespace seams_test

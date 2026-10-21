#pragma once

// Points and bitwise comparisons of the tests that prove an optimization of the noise changes no
// bit: frozen_reference_test, noise_octaves_test, heightfield_batch_test.

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/world_size.hpp"

#include "test_assert.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

namespace bit_exact {

using terrain::WorldSize;

// The default world and a small one, whose wrapping is hit far more often.
inline const std::array<WorldSize, 2> kWorlds = {WorldSize{},
                                                 WorldSize{65536.0, 3.0 * 65536.0}};

struct Point {
    double x;
    double z;
};

// Deterministic coordinates in [-range, range).
class Points {
public:
    double next(double range) {
        state_ = terrain::noise::deriveSeed(state_, 0);
        const double unit = static_cast<double>(state_ >> 11) * 0x1p-53;
        return (2.0 * unit - 1.0) * range;
    }

private:
    std::uint64_t state_ = 11;
};

inline std::vector<Point> randomPoints(std::size_t count, double range) {
    Points points;
    std::vector<Point> result;
    for (std::size_t i = 0; i < count; ++i) {
        const double x = points.next(range);
        result.push_back({x, points.next(range)});
    }
    return result;
}

// Coordinates along an axis of period p that exercise every branch of the wrapping: 0 and -0,
// +-p / 2 and their two neighbors on each side, their images 1, 2 and 1000 periods away, other
// negatives, and coordinates far beyond the world.
inline std::vector<double> edgeValues(double period) {
    const double half = 0.5 * period;
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> base = {0.0, -0.0, 1.0 / 64.0, -1.0 / 64.0, 0.25 * period, -0.3 * period};
    for (const double bound : {half, -half}) {
        double below = bound;
        double above = bound;
        base.push_back(bound);
        for (int step = 0; step < 2; ++step) {
            below = std::nextafter(below, -inf);
            above = std::nextafter(above, inf);
            base.push_back(below);
            base.push_back(above);
        }
        base.push_back(bound - 1.0 / 64.0);
        base.push_back(bound + 1.0 / 64.0);
    }
    std::vector<double> values = base;
    for (const double k : {1.0, -1.0, 2.0, -2.0, 1000.0, -1000.0}) {
        for (const double v : base) {
            values.push_back(v + k * period);
        }
    }
    values.push_back(1e12 + 0.5);
    values.push_back(-1e12 - 0.25);
    return values;
}

inline std::vector<Point> edgePoints(const WorldSize& world) {
    std::vector<Point> points;
    const std::vector<double> xs = edgeValues(world.x);
    const std::vector<double> zs = edgeValues(world.z);
    for (const double z : zs) {
        for (const double x : xs) {
            points.push_back({x, z});
        }
    }
    return points;
}

// Rows of length points, every 1, 37 and 100 m, along x and along z: through the origin, across
// the edges of the world and across its corner, where consecutive points of a batch share
// noise cells and the wrapping changes between them.
inline std::vector<Point> rowPoints(const WorldSize& world, int length) {
    const double hx = 0.5 * world.x;
    const double hz = 0.5 * world.z;
    const std::array<Point, 4> starts = {Point{-20.5, 3.25}, Point{hx, -77.0}, Point{-1000.0, hz},
                                         Point{hx, hz}};
    std::vector<Point> points;
    for (const double spacing : {1.0, 37.0, 100.0}) {
        const double back = 0.5 * spacing * length;
        for (const Point& start : starts) {
            for (int i = 0; i < length; ++i) {
                points.push_back({start.x - back + i * spacing, start.z});
            }
            for (int i = 0; i < length; ++i) {
                points.push_back({start.x, start.z - back + i * spacing});
            }
        }
    }
    return points;
}

// Mismatches of one family of checks, the first one printed.
class Tally {
public:
    explicit Tally(const char* name) : name_(name) {}
    ~Tally() {
        std::printf("%-34s %9ld checks, %ld different\n", name_, checked_, failed_);
        TEST_CHECK(failed_ == 0);
        TEST_CHECK(checked_ > 0);
    }
    Tally(const Tally&) = delete;
    Tally& operator=(const Tally&) = delete;

    void check(bool same, Point p, const char* what) {
        ++checked_;
        if (!same && failed_++ == 0) {
            std::printf("  first difference: %s at (%.17g, %.17g)\n", what, p.x, p.z);
        }
    }

private:
    const char* name_;
    long checked_ = 0;
    long failed_ = 0;
};

inline bool same(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

inline bool same(double a, double b) {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

inline bool same(const terrain::HeightSample& a, const terrain::HeightSample& b) {
    return same(a.continentalness, b.continentalness) &&
           same(a.macroContinentalness, b.macroContinentalness) &&
           same(a.plateContinentalness, b.plateContinentalness) &&
           same(a.plateCollision, b.plateCollision) && same(a.mountainMask, b.mountainMask) &&
           same(a.base, b.base) && same(a.mountain, b.mountain) && same(a.detail, b.detail) &&
           same(a.height, b.height);
}

} // namespace bit_exact

// The size of the wrapped world: validation, exact reduction of coordinates into the canonical
// range, nearest images and periodic grids.
#include "terrain/world_size.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <stdexcept>

namespace {

using terrain::PeriodicCells;
using terrain::WorldSize;

bool throwsInvalid(const WorldSize& world) {
    try {
        terrain::validate(world);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

bool sameBits(double a, double b) {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

void testDefaultSize() {
    const WorldSize world;
    TEST_CHECK(world.x == 916.0 * 65536.0 && world.z == 534.0 * 65536.0);
    TEST_CHECK(std::abs(world.x - 60e6) < 0.001 * 60e6 && std::abs(world.z - 35e6) < 0.001 * 35e6);
    TEST_CHECK(!throwsInvalid(world));
    TEST_CHECK(!throwsInvalid({65536.0, 65536.0}));
    TEST_CHECK(!throwsInvalid({terrain::kMaxWorldMeters, 131072.0}));
}

void testValidate() {
    const double inf = std::numeric_limits<double>::infinity();
    TEST_CHECK(throwsInvalid({60'000'000.0, 34'996'224.0})); // not a multiple of 65,536 m
    TEST_CHECK(throwsInvalid({60'030'976.0, 35'000'000.0}));
    TEST_CHECK(throwsInvalid({0.0, 65536.0}));
    TEST_CHECK(throwsInvalid({-65536.0, 65536.0}));
    TEST_CHECK(throwsInvalid({65536.0, inf}));
    TEST_CHECK(throwsInvalid({std::numeric_limits<double>::quiet_NaN(), 65536.0}));
    TEST_CHECK(throwsInvalid({2.0 * terrain::kMaxWorldMeters, 65536.0}));
}

// Coordinates of several grids: integers, multiples of 1/1024 m, and arbitrary doubles.
double coordinate(int i, int kind, double period) {
    const double t =
        (static_cast<double>(i) * 0.6180339887498949) - std::floor(i * 0.6180339887498949);
    const double v = (t - 0.5) * 1.2 * period; // beyond the canonical range on both sides
    switch (kind) {
    case 0:
        return std::floor(v);
    case 1:
        return std::floor(v * 1024.0) / 1024.0;
    default:
        return v;
    }
}

void testWrapCoordinate() {
    const WorldSize world;
    for (const double period : {world.x, world.z, 65536.0}) {
        const double half = 0.5 * period;
        bool inRange = true;
        bool exact = true;
        bool periodic = true;
        for (int i = 0; i < 20000; ++i) {
            for (int kind = 0; kind < 3; ++kind) {
                const double v = coordinate(i, kind, period);
                const double w = terrain::wrapCoordinate(v, period);
                inRange = inRange && w >= -half && w < half;
                const double turns = (v - w) / period;
                exact = exact && turns == std::round(turns);
                if (kind < 2) { // v + k * period is exact on these grids
                    for (const double k : {-3.0, -1.0, 1.0, 2.0, 1000.0}) {
                        periodic = periodic &&
                                   sameBits(terrain::wrapCoordinate(v + k * period, period), w);
                    }
                }
            }
        }
        TEST_CHECK(inRange);
        TEST_CHECK(exact);
        TEST_CHECK(periodic);
        TEST_CHECK(terrain::wrapCoordinate(half, period) == -half);
        TEST_CHECK(terrain::wrapCoordinate(-half, period) == -half);
        TEST_CHECK(terrain::wrapCoordinate(std::nextafter(half, 0.0), period) ==
                   std::nextafter(half, 0.0));
        TEST_CHECK(terrain::wrapCoordinate(std::nextafter(-half, -period), period) ==
                   std::nextafter(-half, -period) + period);
        TEST_CHECK(terrain::wrapCoordinate(0.25, period) == 0.25);
        TEST_CHECK(
            std::isnan(terrain::wrapCoordinate(std::numeric_limits<double>::infinity(), period)));
    }
}

void testNearestImage() {
    const double period = WorldSize{}.x;
    for (int i = 0; i < 1000; ++i) {
        const double v = coordinate(i, 0, period);
        const double reference = coordinate(i + 7919, 1, period);
        const double image = terrain::nearestImage(v, reference, period);
        TEST_CHECK(std::abs(image - reference) <= 0.5 * period);
        const double turns = (image - v) / period;
        TEST_CHECK(turns == std::round(turns));
    }
    TEST_CHECK(terrain::nearestImage(-0.5 * period + 16.0, 0.5 * period - 8.0, period) ==
               0.5 * period + 16.0);
}

void testWrapIndex() {
    for (const std::int64_t count : {1, 2, 5, 8, 3751936}) {
        const std::int64_t low = -(count / 2);
        const std::int64_t high = count - count / 2;
        for (std::int64_t i = -3 * count - 2; i <= 3 * count + 2;
             i += std::max<std::int64_t>(1, count / 7)) {
            const std::int64_t w = terrain::wrapIndex(i, count);
            TEST_CHECK(w >= low && w < high);
            TEST_CHECK((i - w) % count == 0);
            TEST_CHECK(terrain::wrapIndex(i + count, count) == w);
        }
        for (std::int64_t i = low; i < high && i < low + 100; ++i) {
            TEST_CHECK(terrain::wrapIndex(i, count) == i);
        }
    }
}

void testPeriodicCells() {
    const WorldSize world;
    // Powers of two divide the period: their cells keep their nominal size exactly.
    for (const double nominal : {4.0, 16.0, 128.0, 256.0}) {
        const PeriodicCells cells = terrain::periodicCells(world.z, nominal);
        TEST_CHECK(cells.size == nominal);
        TEST_CHECK(static_cast<double>(cells.count) * cells.size == world.z);
    }
    // Other sizes are rounded to a whole number of cells.
    for (const double nominal : {3.0, 192.0, 400.0, 3000.0, 1e9}) {
        for (const double period : {world.x, world.z}) {
            const PeriodicCells cells = terrain::periodicCells(period, nominal);
            TEST_CHECK(cells.count == std::max<std::int64_t>(1, static_cast<std::int64_t>(
                                                                    std::round(period / nominal))));
            TEST_CHECK(cells.size == period / static_cast<double>(cells.count));
            TEST_CHECK(std::abs(cells.size - nominal) <=
                           0.5 * nominal / static_cast<double>(cells.count) + 1e-9 ||
                       cells.count == 1);
        }
    }
    const PeriodicCells cells = terrain::periodicCells(world.x, 192.0);
    TEST_CHECK(cells.count == 312661);
    for (const std::int64_t cell :
         {std::int64_t{-5}, std::int64_t{0}, std::int64_t{156330}, std::int64_t{-156330}}) {
        TEST_CHECK(cells.wrap(cell) == cell); // the canonical range holds the cells near the origin
        TEST_CHECK(cells.shift(cell) == 0.0);
        TEST_CHECK(cells.wrap(cell + cells.count) == cell);
        TEST_CHECK(cells.shift(cell + cells.count) == world.x);
        TEST_CHECK(cells.shift(cell - 2 * cells.count) == -2.0 * world.x);
    }
    TEST_CHECK(cells.cellOf(0.0) == 0 && cells.cellOf(-0.001) == -1);
    TEST_CHECK(cells.cellOf(cells.size * 10.5) == 10);
    bool threw = false;
    try {
        (void)terrain::periodicCells(world.x, 0.0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    TEST_CHECK(threw);
}

} // namespace

int main() {
    testDefaultSize();
    testValidate();
    testWrapCoordinate();
    testNearestImage();
    testWrapIndex();
    testPeriodicCells();
    return test::result();
}

#include "terrain/world_size.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace terrain {

namespace {

bool aligned(double side) {
    return std::isfinite(side) && side >= kWorldAlignmentMeters && side <= kMaxWorldMeters &&
           side == kWorldAlignmentMeters * std::floor(side / kWorldAlignmentMeters);
}

} // namespace

void validate(const WorldSize& world) {
    if (!aligned(world.x) || !aligned(world.z)) {
        throw std::invalid_argument("WorldSize: sides must be multiples of 65,536 m up to 2^30 m, "
                                    "not " +
                                    std::to_string(world.x) + " x " + std::to_string(world.z));
    }
}

double wrapCoordinate(double v, double period) {
    // The bounds k * period +- period / 2 are exact (period is a multiple of 2^16), so comparing
    // v with them corrects any rounding of v / period; then v - k * period is exact, as v and
    // k * period are multiples of the unit in the last place of v and the result is smaller.
    const double half = 0.5 * period;
    double k = std::floor(v / period + 0.5);
    if (v < k * period - half) {
        k -= 1.0;
    } else if (v >= k * period + half) {
        k += 1.0;
    }
    return v - k * period;
}

double nearestImage(double v, double reference, double period) {
    return v + period * std::floor((reference - v) / period + 0.5);
}

std::int64_t wrapIndex(std::int64_t i, std::int64_t count) {
    const std::int64_t half = count / 2;
    std::int64_t shifted = (i + half) % count;
    if (shifted < 0) {
        shifted += count;
    }
    return shifted - half;
}

std::int64_t PeriodicCells::cellOf(double v) const {
    return static_cast<std::int64_t>(std::floor(v / size));
}

double PeriodicCells::shift(std::int64_t cell) const {
    return static_cast<double>((cell - wrap(cell)) / count) * period;
}

PeriodicCells periodicCells(double period, double nominal) {
    if (!std::isfinite(period) || !std::isfinite(nominal) || period <= 0.0 || nominal <= 0.0) {
        throw std::invalid_argument("periodicCells: period and size must be finite and above 0");
    }
    const double count = std::max(1.0, std::round(period / nominal));
    return {static_cast<std::int64_t>(count), period / count, period};
}

} // namespace terrain

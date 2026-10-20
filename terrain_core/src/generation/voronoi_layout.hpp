#pragma once

// Layout of the tectonic plates, shared by voronoi.cpp and voronoi_layout.cpp only.

#include "terrain/generation/voronoi.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace terrain::voronoi_layout {

// Offset along one axis of a torus of side period, in [-period / 2, period / 2]: the
// shortest way from a coordinate to another one. Continuous wherever |delta| < period / 2.
[[nodiscard]] inline double wrapOffset(double delta, double period) {
    return delta - period * std::round(delta / period);
}

// Coordinate brought back into [0, period).
[[nodiscard]] inline double wrapPosition(double v, double period) {
    const double wrapped = v - period * std::floor(v / period);
    return wrapped < period ? wrapped : 0.0;
}

// wrapOffset for a delta between two coordinates of [0, period), without any rounding.
[[nodiscard]] inline double shortOffset(double delta, double period) {
    if (delta > 0.5 * period) {
        return delta - period;
    }
    return delta < -0.5 * period ? delta + period : delta;
}

// Nearest site to (x, z) on the torus, the lowest index on a tie.
[[nodiscard]] int nearestSite(const std::vector<Plate>& plates, double x, double z, double period);

struct Layout {
    double planetSize = 0.0;
    std::vector<Plate> plates;
    std::vector<PlateBoundary> boundaries; // sorted by (a, b)
};

// Sites (best candidate sampling then Lloyd relaxation), exact Voronoi neighbors, plate
// velocities and boundary kinds, then plate kinds. Assumes valid parameters.
[[nodiscard]] Layout buildLayout(std::uint64_t seed, const VoronoiParams& params);

} // namespace terrain::voronoi_layout

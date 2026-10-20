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

// wrapOffset for a delta between two coordinates of the canonical range, without any rounding.
[[nodiscard]] inline double shortOffset(double delta, double period) {
    if (delta > 0.5 * period) {
        return delta - period;
    }
    return delta < -0.5 * period ? delta + period : delta;
}

// Nearest site to (x, z) on the torus of the world, the lowest index on a tie.
[[nodiscard]] int nearestSite(const std::vector<Plate>& plates, double x, double z,
                              const WorldSize& world);

struct Layout {
    double cellDiameter = 0.0; // mean equivalent diameter of a cell, 2 sqrt(area / pi)
    std::vector<Plate> plates;
    std::vector<PlateBoundary> boundaries; // sorted by (a, b)
};

// Sites (best candidate sampling then Lloyd relaxation), exact Voronoi neighbors, plate
// velocities and boundary kinds, then plate kinds. Assumes valid parameters and world.
[[nodiscard]] Layout buildLayout(std::uint64_t seed, const VoronoiParams& params,
                                 const WorldSize& world);

} // namespace terrain::voronoi_layout

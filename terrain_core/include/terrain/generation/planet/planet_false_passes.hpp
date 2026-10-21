#pragma once

#include "terrain/generation/planet/planet_rivers.hpp"

#include <array>
#include <cstdint>
#include <functional>

namespace terrain {

// How far the fine relief rises above the rivers of the planet level ("false passes"): at 8 km
// per cell, a river can cross a range between two crests that the coarse grid missed, and 11B
// would have to cut a canyon there. Lengths in meters.
struct FalsePassStats {
    double sampled = 0.0; // length of river sampled
    double over50 = 0.0;  // length where the relief is more than 50 m above the river
    double over200 = 0.0;
    double over500 = 0.0;
    float maxExcess = 0.0f; // meters
    double maxX = 0.0;      // where
    double maxZ = 0.0;
    // Per Strahler order (index = order, 0 unused, the last one for all higher orders).
    std::array<double, 8> sampledByOrder{};
    std::array<double, 8> over200ByOrder{};
};

namespace planet {

// Samples the fine relief along every segment about every step meters (each edge of a polyline
// cut into ceil(length / step) equal parts, sampled at their middles) and compares it with the
// profile of the river: the altitudes of its points made non-increasing downstream, linear
// between them. Segments in fixed blocks on up to threads threads, lengths counted in integer
// millimeters and added in segment order: the same result whatever the number of threads.
[[nodiscard]] FalsePassStats
measureFalsePasses(const RiverGraph& graph, const std::function<float(double x, double z)>& relief,
                   int threads, double step = 500.0);

} // namespace planet

} // namespace terrain

#pragma once

// Vertical scans of the carved density of a column, shared by the objects laid in the caves
// (speleothems, cave flora): internal to terrain_core.

#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/caves.hpp"

#include <cmath>
#include <optional>

namespace terrain::cave_scan {

// A void under a column: its ceiling and floor, where the carved density crosses 0.
struct Span {
    float ceiling;
    float floor;
};

// Height where the density crosses 0 between y (density a) and y - 1 (density b).
inline float crossing(double y, float a, float b) {
    return static_cast<float>(y - static_cast<double>(a / (a - b)));
}

// Highest void at least minSpan high under the column, below its ground, that is not open to the
// sky: the scan goes down every meter from 1 m under the ground and skips the void it starts in.
inline std::optional<Span> highestSpan(CaveSampler& sampler, const CaveColumn& column,
                                       float minSpan) {
    const CaveParams& params = sampler.field().params();
    const double top = std::ceil(static_cast<double>(column.ground)) - 1.0;
    const double bottom = static_cast<double>(column.ground) - params.maxDepth - 2.0;
    const auto at = [&](double y) {
        const auto terrain = static_cast<float>(y - static_cast<double>(column.ground));
        return sampler.density(column, y, terrain);
    };
    float previous = at(top);
    bool openToSky = previous >= 0.0f;
    std::optional<float> ceiling;
    for (double y = top - 1.0; y >= bottom; y -= 1.0) {
        const float density = at(y);
        if (previous < 0.0f && density >= 0.0f) {
            ceiling = crossing(y + 1.0, previous, density);
        } else if (previous >= 0.0f && density < 0.0f) {
            const float floor = crossing(y + 1.0, previous, density);
            if (!openToSky && ceiling && *ceiling - floor >= minSpan) {
                return Span{*ceiling, floor};
            }
            openToSky = false;
            ceiling.reset();
        }
        previous = density;
    }
    return std::nullopt;
}

} // namespace terrain::cave_scan

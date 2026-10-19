#pragma once

#include <algorithm>

// Building blocks of signed distance fields: negative inside a shape, zero on its surface,
// positive outside. Shapes are combined with min / max, which keeps the result continuous.
namespace terrain::sdf {

// Inside either shape.
[[nodiscard]] inline float unite(float a, float b) {
    return std::min(a, b);
}

// Inside both shapes.
[[nodiscard]] inline float intersect(float a, float b) {
    return std::max(a, b);
}

// Inside a but not inside b: b is carved out of a.
[[nodiscard]] inline float subtract(float a, float b) {
    return std::max(a, -b);
}

// Vertical prism of a horizontal footprint, below height top. footprint is the signed
// horizontal distance to the outline of the footprint (negative inside). The sides of the
// prism are exactly vertical and its top exactly flat.
[[nodiscard]] inline float prismBelow(float footprint, double y, double top) {
    return intersect(footprint, static_cast<float>(y - top));
}

// Vertical slot of half-width halfWidth around an axis at horizontal distance distance,
// above height floor: a channel, a gorge or a cirque with vertical walls and a flat floor.
[[nodiscard]] inline float slotAbove(float distance, float halfWidth, double y, double floor) {
    return intersect(distance - halfWidth, static_cast<float>(floor - y));
}

} // namespace terrain::sdf

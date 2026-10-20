#pragma once

#include <algorithm>
#include <cmath>

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

// Smooth maximum: max(a, b) + h^2 / (4 k) with h = max(k - |a - b|, 0). Exactly max(a, b) when
// |a - b| >= k, never below it and at most k / 4 above it; continuous with slope at most 1 in
// each argument. k <= 0 gives max(a, b).
[[nodiscard]] inline float smoothMax(float a, float b, float k) {
    const float difference = std::abs(a - b);
    if (k <= 0.0f || difference >= k) {
        return std::max(a, b);
    }
    const float h = k - difference;
    return std::max(a, b) + h * h / (4.0f * k);
}

// Smooth minimum, the mirror: min(a, b) - h^2 / (4 k).
[[nodiscard]] inline float smoothMin(float a, float b, float k) {
    const float difference = std::abs(a - b);
    if (k <= 0.0f || difference >= k) {
        return std::min(a, b);
    }
    const float h = k - difference;
    return std::min(a, b) - h * h / (4.0f * k);
}

// b carved out of a with a rounded seam: smoothMax(a, -b, k). Never more solid than a.
[[nodiscard]] inline float smoothSubtract(float a, float b, float k) {
    return smoothMax(a, -b, k);
}

// Union with a rounded seam: smoothMin(a, b, k). Never less solid than either shape.
[[nodiscard]] inline float smoothUnite(float a, float b, float k) {
    return smoothMin(a, b, k);
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

// Vertical cylinder of radius radius around an axis at horizontal distance radial, between
// heights bottom and top: a pillar with a flat base and a flat top.
[[nodiscard]] inline float verticalCylinder(float radial, float radius, double y, double bottom,
                                            double top) {
    return intersect(radial - radius,
                     intersect(static_cast<float>(bottom - y), static_cast<float>(y - top)));
}

// Torus of tube radius minorRadius around a circle of radius majorRadius: radial is the
// distance from the axis of the torus, axial the signed coordinate along it.
[[nodiscard]] inline float torus(float radial, float axial, float majorRadius,
                                 float minorRadius) {
    return std::hypot(radial - majorRadius, axial) - minorRadius;
}

} // namespace terrain::sdf

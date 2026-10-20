#pragma once

#include <cstdint>

namespace terrain::unreal {

// A vertex color, one byte per channel, as Unreal's FColor.
struct VertexColor {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    friend bool operator==(const VertexColor&, const VertexColor&) = default;
};

// Bands of the altitude color, meters: the sea floor from kSeabedDepth (blue gray) to the sea
// level (sand), the beach up to kBeachTop (sand to green), the land up to kRockAltitude (green
// to gray, as the streamed world of the viewer), the rock up to kSnowStart and the snow up to
// kSnowFull (gray to white). Beyond the first and last bands the color stays the same.
inline constexpr double kSeabedDepth = -500.0;
inline constexpr double kBeachTop = 20.0;
inline constexpr double kRockAltitude = 2500.0;
inline constexpr double kSnowStart = 3000.0;
inline constexpr double kSnowFull = 3500.0;

// Color of the ground at this altitude, meters, linear between the bands: a readable relief
// until the materials of phase 14. Opaque.
[[nodiscard]] VertexColor altitudeColor(double altitude);

} // namespace terrain::unreal

#include "altitude_color.hpp"

#include "terrain/generation/heightfield.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace terrain::unreal {

namespace {

struct ColorKey {
    double altitude;
    double r;
    double g;
    double b;
};

// The land colors are the ones of the streamed world of the viewer (its fragment shader).
constexpr std::array<ColorKey, 6> kKeys{{
    {kSeabedDepth, 0.20, 0.25, 0.32},
    {kSeaLevel, 0.76, 0.70, 0.50},
    {kBeachTop, 0.30, 0.55, 0.25},
    {kRockAltitude, 0.65, 0.60, 0.55},
    {kSnowStart, 0.65, 0.60, 0.55},
    {kSnowFull, 0.95, 0.95, 0.97},
}};

// Dry ground below the sea level, from the sea level down: the sand of the sea level, then the
// salt and clay of the dev map (196, 168, 128 at -100 m; 150, 112, 82 at kDryBelowDepth).
constexpr std::array<ColorKey, 3> kDryKeys{{
    {kSeaLevel, 0.76, 0.70, 0.50},
    {-100.0, 196.0 / 255.0, 168.0 / 255.0, 128.0 / 255.0},
    {kDryBelowDepth, 150.0 / 255.0, 112.0 / 255.0, 82.0 / 255.0},
}};

std::uint8_t toByte(double value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
}

VertexColor colorOf(double r, double g, double b) {
    return {toByte(r), toByte(g), toByte(b), 255};
}

} // namespace

VertexColor altitudeColor(double altitude) {
    if (!(altitude > kKeys.front().altitude)) { // NaN included
        return colorOf(kKeys.front().r, kKeys.front().g, kKeys.front().b);
    }
    for (std::size_t i = 1; i < kKeys.size(); ++i) {
        const ColorKey& low = kKeys[i - 1];
        const ColorKey& high = kKeys[i];
        if (altitude <= high.altitude) {
            const double t = (altitude - low.altitude) / (high.altitude - low.altitude);
            return colorOf(low.r + t * (high.r - low.r), low.g + t * (high.g - low.g),
                           low.b + t * (high.b - low.b));
        }
    }
    return colorOf(kKeys.back().r, kKeys.back().g, kKeys.back().b);
}

VertexColor groundColor(double altitude, bool sea) {
    if (sea || !(altitude < kSeaLevel)) {
        return altitudeColor(altitude);
    }
    // Dry ground below the sea level: clay and salt, from the sand of the sea level down to
    // kDryBelowDepth (the tints of the dev map, map_style.cpp kBelowSea), never the sea floor.
    for (std::size_t i = 1; i < kDryKeys.size(); ++i) {
        const ColorKey& high = kDryKeys[i - 1];
        const ColorKey& low = kDryKeys[i];
        if (altitude >= low.altitude) {
            const double t = (altitude - low.altitude) / (high.altitude - low.altitude);
            return colorOf(low.r + t * (high.r - low.r), low.g + t * (high.g - low.g),
                           low.b + t * (high.b - low.b));
        }
    }
    return colorOf(kDryKeys.back().r, kDryKeys.back().g, kDryKeys.back().b);
}

} // namespace terrain::unreal

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
    const bool dryBelow = !sea && altitude < kSeaLevel;
    return altitudeColor(dryBelow ? static_cast<double>(kSeaLevel) : altitude);
}

} // namespace terrain::unreal

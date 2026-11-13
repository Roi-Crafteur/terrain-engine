#include "terrain/generation/monsoon.hpp"

#include "terrain/generation/noise.hpp"

#include <cmath>
#include <stdexcept>

namespace terrain {

namespace {

constexpr std::uint64_t kWarpStream = 0x4D4F4E53; // "MONS"

bool ordered(double low, double high) {
    return low < high;
}

} // namespace

Monsoon::Monsoon(std::uint64_t seed, const WorldSize& world, const MonsoonParams& params)
    : params_(params), world_(world), warpSeed_(noise::deriveSeed(seed, kWarpStream)) {
    validate(world_);
    const MonsoonParams& p = params_;
    if (!ordered(p.warmLow, p.warmHigh) || !ordered(p.wetLow, p.wetHigh) ||
        !ordered(p.bandInner, p.bandOuter) || !(p.bandEdge > 0.0) || !(p.warpDegrees >= 0.0) ||
        !(p.warpWavelength > 0.0)) {
        throw std::invalid_argument("MonsoonParams: invalid ranges");
    }
}

double Monsoon::latitudeAt(double x, double z) const {
    const noise::FbmParams warp{.octaves = 2, .frequency = 1.0 / params_.warpWavelength};
    const double moved = params_.warpDegrees / 180.0 * world_.z *
                         noise::fbm2D(x, z, warpSeed_, warp, world_);
    // The canonical z lies within [-world.z / 2, world.z / 2): the poles at its ends.
    return 180.0 * std::abs(wrapCoordinate(z + moved, world_.z)) / world_.z;
}

float Monsoon::shareAt(double x, double z, const ClimateSample& climate) const {
    const MonsoonParams& p = params_;
    const float warm = noise::smoothstep(p.warmLow, p.warmHigh, climate.baseTemperature);
    const float wet = noise::smoothstep(p.wetLow, p.wetHigh, climate.humidity);
    if (!(warm > 0.0f) || !(wet > 0.0f)) {
        return 0.0f;
    }
    const auto latitude = static_cast<float>(latitudeAt(x, z));
    const auto inner = static_cast<float>(p.bandInner);
    const auto outer = static_cast<float>(p.bandOuter);
    const auto edge = static_cast<float>(p.bandEdge);
    const float band = noise::smoothstep(inner - edge, inner, latitude) *
                       (1.0f - noise::smoothstep(outer, outer + edge, latitude));
    return warm * wet * band;
}

} // namespace terrain

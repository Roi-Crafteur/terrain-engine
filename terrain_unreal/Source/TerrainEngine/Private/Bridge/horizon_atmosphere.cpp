#include "horizon_atmosphere.hpp"

#include "horizon_layout.hpp"

#include "terrain/generation/heightfield.hpp"

#include <cmath>

namespace terrain::unreal {

namespace {

// v modulo period, in [0, period): fmod is exact.
double positiveModulo(double v, double period) {
    const double r = std::fmod(v, period);
    return r < 0.0 ? r + period : r;
}

} // namespace

double seaLevelZ(const UnrealFrame& frame) {
    return toUnreal(frame, {0.0, static_cast<double>(kSeaLevel), 0.0}).z;
}

UnrealPoint planetTopUnder(const UnrealPoint& camera, const UnrealFrame& frame) {
    return {camera.x, camera.y, seaLevelZ(frame)};
}

double heightAbovePlanet(const UnrealPoint& camera, const UnrealPoint& planetTop,
                         double radiusMeters) {
    const double dx = (camera.x - planetTop.x) / kCentimetersPerMeter;
    const double dy = (camera.y - planetTop.y) / kCentimetersPerMeter;
    const double dz = (camera.z - planetTop.z) / kCentimetersPerMeter;
    // |c - center| - R, written without the cancellation of two numbers near R.
    const double horizontal = dx * dx + dy * dy;
    const double up = radiusMeters + dz;
    return (horizontal + dz * dz + 2.0 * radiusMeters * dz) /
           (std::sqrt(horizontal + up * up) + radiusMeters);
}

double aerialPerspectiveDepthKm(const HorizonParams& params) {
    return std::ceil(levelReachMeters(params, params.levels - 1) / 1000.0);
}

WaveOrigin seaWaveOrigin(const UnrealFrame& frame) {
    const UnrealPoint coreOrigin = toUnreal(frame, {0.0, 0.0, 0.0});
    const double period = kSeaWavePeriodMeters * kCentimetersPerMeter;
    return {positiveModulo(coreOrigin.x, period), positiveModulo(coreOrigin.y, period)};
}

} // namespace terrain::unreal

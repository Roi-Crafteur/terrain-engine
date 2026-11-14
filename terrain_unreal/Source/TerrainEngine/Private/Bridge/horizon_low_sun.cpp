#include "horizon_low_sun.hpp"

#include <cmath>
#include <numbers>

namespace terrain::unreal {

double sunElevationDegrees(double dirX, double dirY, double dirZ) {
    const double length = std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
    if (length <= 0.0) {
        return 0.0;
    }
    return std::asin(-dirZ / length) * 180.0 / std::numbers::pi;
}

int lowSunShadowLevels(double sunElevation, double belowDegrees, int current) {
    if (belowDegrees <= 0.0) {
        return 0;
    }
    if (sunElevation < belowDegrees) {
        return kLowSunShadowLevels;
    }
    if (sunElevation > belowDegrees + kLowSunHysteresisDegrees) {
        return 0;
    }
    return current == kLowSunShadowLevels ? kLowSunShadowLevels : 0;
}

} // namespace terrain::unreal

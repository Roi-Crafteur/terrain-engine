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

ShadowCache shadowCacheOf(int level, int shadowLevels, bool everyLevel, bool rigid) {
    const bool casting = everyLevel || level < shadowLevels;
    return rigid && casting ? ShadowCache::Rigid : ShadowCache::Auto;
}

const char* shadowCacheName(ShadowCache cache) {
    return cache == ShadowCache::Rigid ? "Rigid" : "Auto";
}

std::uint32_t shadowCacheStale(std::span<const int> uploaded, int levels, int shadowLevels,
                               bool everyLevel, bool rigid) {
    std::uint32_t stale = 0;
    for (const int level : uploaded) {
        for (const int shaped : {level, level - 1}) {
            if (shaped >= 0 && shaped < levels && shaped < 32 &&
                shadowCacheOf(shaped, shadowLevels, everyLevel, rigid) == ShadowCache::Rigid) {
                stale |= std::uint32_t{1} << shaped;
            }
        }
    }
    return stale;
}

} // namespace terrain::unreal

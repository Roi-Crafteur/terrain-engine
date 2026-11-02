#include "horizon_surface.hpp"

#include <algorithm>
#include <cmath>

namespace terrain::unreal {

std::optional<SurfaceWindow> surfaceAfter(const std::optional<SurfaceWindow>& surface,
                                          const LevelPlacement& placement) {
    if (placement.level != 0) {
        return surface;
    }
    if (!placement.visible) {
        return std::nullopt;
    }
    return SurfaceWindow{placement.origin, placement.firstSlot};
}

double horizonWaterShare(bool hasSurface, double relX, double relY, double cells, double pitchCm,
                         double coveredCm, double bandCm) {
    if (!hasSurface || !(pitchCm > 0.0)) {
        return 0.0;
    }
    const double i = relX / pitchCm;
    const double j = relY / pitchCm;
    if (!(i >= 0.0 && j >= 0.0 && i <= cells && j <= cells)) {
        return 0.0; // outside the window of level 0
    }
    const double fromCenter =
        std::max(std::abs(i - cells * 0.5), std::abs(j - cells * 0.5)) * pitchCm;
    const double fade = std::max(0.5 * bandCm, 1.0);
    return std::clamp((fromCenter - (coveredCm - fade)) / fade, 0.0, 1.0);
}

} // namespace terrain::unreal

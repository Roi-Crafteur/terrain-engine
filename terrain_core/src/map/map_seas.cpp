#include "terrain/map/map_seas.hpp"

#include "terrain/generation/seas.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include <algorithm>
#include <utility>

namespace terrain {
namespace {

// A ridge axis is drawn kAxisPixels wide at least, kAxisMeters at a fine zoom.
constexpr double kAxisPixels = 1.5;
constexpr double kAxisMeters = 12'000.0;
// The least trench, fracture and seamount drawn, meters; the full color from the second.
constexpr double kTrenchLow = 300.0;
constexpr double kTrenchFull = 3'000.0;
constexpr double kFractureLow = 100.0;
constexpr double kSeamountLow = 300.0;

MapColor alpha(MapColor c, double share) {
    c.a = static_cast<std::uint8_t>(std::clamp(share, 0.0, 1.0) * 255.0);
    return c;
}

} // namespace

MapLayerFn seasLayer(std::shared_ptr<const Hydrology> hydrology) {
    return [hydrology = std::move(hydrology)](const MapPoint& p) -> MapColor {
        const Seas* seas = hydrology != nullptr ? hydrology->seas() : nullptr;
        if (seas == nullptr || p.at().water != WaterKind::Ocean) {
            return {};
        }
        const double pixel = std::max(p.stepX, p.stepZ);
        const SeaPoint s = seas->mapAt(p.x, p.z, pixel);
        if (!s.zone) {
            return {};
        }
        if (s.seamount > kSeamountLow) {
            return alpha(kMapSeamountColor, 0.9);
        }
        if (s.ridge >= 0 && s.ridgeDistance < std::max(kAxisMeters, kAxisPixels * pixel)) {
            return alpha(kMapRidgeColor, 0.85);
        }
        if (s.fracture > kFractureLow) {
            return alpha(kMapFractureColor, 0.7);
        }
        if (s.trench > kTrenchLow) {
            return alpha(kMapTrenchColor,
                         0.3 + 0.6 * (s.trench - kTrenchLow) / (kTrenchFull - kTrenchLow));
        }
        if (s.plateau > 0.5) {
            return alpha(kMapPlateauColor, 0.45);
        }
        return {};
    };
}

} // namespace terrain

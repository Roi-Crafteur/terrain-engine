#include "terrain/map/map_ergs.hpp"

#include "terrain/generation/ergs.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include <algorithm>
#include <utility>

namespace terrain {

namespace {

// The alpha of the layer: kAlphaLow where the mask starts, kAlphaLow + kAlphaRange where it is full.
constexpr float kAlphaLow = 40.0f;
constexpr float kAlphaRange = 110.0f;

} // namespace

MapColor mapDuneColor(DuneKind kind) {
    switch (kind) {
    case DuneKind::Barchan:
        return {250, 226, 160, 255};
    case DuneKind::Transverse:
        return {238, 178, 92, 255};
    case DuneKind::Draa:
        return {196, 112, 44, 255};
    case DuneKind::Seif:
        return {224, 120, 120, 255};
    case DuneKind::Star:
        return {160, 84, 170, 255};
    case DuneKind::None:
        break;
    }
    return {0, 0, 0, 0};
}

MapLayerFn ergsLayer(std::shared_ptr<const Hydrology> hydrology) {
    return [hydrology = std::move(hydrology)](const MapPoint& p) -> MapColor {
        const Ergs* ergs = hydrology != nullptr ? hydrology->ergs() : nullptr;
        if (ergs == nullptr || p.at().water == WaterKind::Ocean || !ergs->mayHold(p.x, p.z)) {
            return {};
        }
        const float relief = hydrology->relief().sampleAt(p.x, p.z).relief;
        const ErgPoint e = ergs->at(p.x, p.z, relief);
        if (!(e.mask > 0.0f)) {
            return {};
        }
        MapColor c = mapDuneColor(ergs->kindOf(e));
        c.a = static_cast<std::uint8_t>(std::clamp(kAlphaLow + kAlphaRange * e.mask, 0.0f, 255.0f));
        return c;
    };
}

} // namespace terrain

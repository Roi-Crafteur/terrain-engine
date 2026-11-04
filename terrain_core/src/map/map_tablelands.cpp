#include "terrain/map/map_tablelands.hpp"

#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/tablelands.hpp"

#include <algorithm>
#include <utility>

namespace terrain {

namespace {

// The alpha of the layer: kAlphaLow where a province starts, kAlphaLow + kAlphaRange where full.
constexpr float kAlphaLow = 70.0f;
constexpr float kAlphaRange = 110.0f;
// A province shows from kShown of its share; its style rules from kStyle.
constexpr float kShown = 0.02f;
constexpr float kStyle = 0.5f;

std::uint8_t alphaOf(float share) {
    return static_cast<std::uint8_t>(std::clamp(kAlphaLow + kAlphaRange * share, 0.0f, 255.0f));
}

} // namespace

MapLayerFn tablelandsLayer(const Heightfield& field) {
    return [field](const MapPoint& p) -> MapColor {
        if (field.tablelands() == nullptr || p.at().water == WaterKind::Ocean) {
            return {};
        }
        const TablelandPoint t = field.tablelandAt(p.x, p.z);
        const float share = std::max(t.tableland, t.karst);
        if (!(share > kShown)) {
            return {};
        }
        MapColor c;
        if (t.karst >= t.tableland) {
            c = t.tropical > kStyle ? MapColor{225, 40, 165, 255} : MapColor{235, 205, 40, 255};
        } else {
            c = t.arid > kStyle ? MapColor{235, 80, 20, 255} : MapColor{140, 70, 190, 255};
        }
        c.a = alphaOf(share);
        return c;
    };
}

MapLayerFn tablelandErrorsLayer(const Heightfield& field, std::shared_ptr<const PlanetLevel> planet) {
    return [field, planet = std::move(planet)](const MapPoint& p) -> MapColor {
        if (planet == nullptr || field.tablelands() == nullptr ||
            p.at().water == WaterKind::Ocean) {
            return {};
        }
        const TablelandPoint t = field.tablelandAt(p.x, p.z);
        if (!t.any()) {
            return {};
        }
        const float humidity = planet->humidityAt(p.x, p.z);
        if (t.karst > kStyle && humidity < kKarstDryHumidity) {
            return {220, 40, 40, alphaOf(t.karst)};
        }
        if (t.tableland > kStyle && t.arid > kStyle && humidity > kMesaHumidHumidity) {
            return {40, 80, 220, alphaOf(t.tableland)};
        }
        return {};
    };
}

} // namespace terrain

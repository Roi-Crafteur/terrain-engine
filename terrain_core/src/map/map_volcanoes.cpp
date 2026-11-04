#include "terrain/map/map_volcanoes.hpp"

#include "../generation/planet/planet_parallel.hpp"
#include "terrain/det_math.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace terrain {

namespace {

// The alpha of the layer: kAlphaLow where the volcanoes start to make the ground, kAlphaLow +
// kAlphaRange where they make it all.
constexpr int kAlphaLow = 50;
constexpr int kAlphaRange = 100;

} // namespace

MapColor mapVolcanoColor(VolcanoForm form) {
    switch (form) {
    case VolcanoForm::Strato:
    case VolcanoForm::TwinStrato:
        return {200, 40, 30, 255};
    case VolcanoForm::CollapsedStrato:
        return {150, 20, 60, 255};
    case VolcanoForm::Shield:
        return {230, 120, 30, 255};
    case VolcanoForm::CinderField:
        return {120, 60, 40, 255};
    case VolcanoForm::LavaField:
    case VolcanoForm::FissureRow:
        return {40, 30, 30, 255};
    case VolcanoForm::MaarField:
        return {60, 120, 160, 255};
    case VolcanoForm::Caldera:
    case VolcanoForm::PlateauCaldera:
        return {170, 60, 170, 255};
    case VolcanoForm::IslandStrato:
    case VolcanoForm::IslandShield:
    case VolcanoForm::IslandCaldera:
        return {250, 200, 40, 255};
    case VolcanoForm::Seamount:
        return {40, 200, 200, 255};
    }
    return {0, 0, 0, 0};
}

MapColor volcanoesLayer(const MapPoint& point) {
    const MapSample& s = point.at();
    if (s.volcano == 0 || s.volcanic == 0) {
        return {};
    }
    MapColor c = mapVolcanoColor(static_cast<VolcanoForm>(s.volcano - 1));
    c.a = static_cast<std::uint8_t>(
        std::clamp(kAlphaLow + kAlphaRange * static_cast<int>(s.volcanic) / 255, 0, 255));
    return c;
}

void markVolcanoes(const Volcanoes& volcanoes, const MapFrame& frame, std::span<MapSample> out,
                   bool cells, int threads) {
    const auto columns = static_cast<std::size_t>(frame.width() + 2);
    const double pixel = 0.6 * std::max(frame.stepX(), frame.stepZ());
    const WorldSize& w = volcanoes.world();
    planet::parallelRows(frame.height() + 2, planet::resolveThreads(threads), [&](int begin,
                                                                                  int end) {
        for (int row = begin; row < end; ++row) {
            const double z = wrapCoordinate(frame.pixelZ(row - 1), w.z);
            for (std::size_t i = 0; i < columns; ++i) {
                const double x =
                    wrapCoordinate(frame.pixelX(static_cast<std::int64_t>(i) - 1), w.x);
                double nearest = std::numeric_limits<double>::infinity();
                MapSample& s = out[static_cast<std::size_t>(row) * columns + i];
                for (const std::uint32_t k : volcanoes.near(x, z)) {
                    const VolcanoSite& v = volcanoes.sites()[k];
                    const VolcanoOffset o = volcanoOffset(w, v.x, v.z, x, z);
                    const double d = dmath::hypot(o.dx, o.dz);
                    const double share = d / static_cast<double>(v.reach);
                    if (!(share < 1.0) || !(share < nearest)) {
                        continue;
                    }
                    nearest = share;
                    s.volcano = static_cast<std::uint8_t>(static_cast<int>(v.form) + 1);
                    const double foot =
                        cells ? static_cast<double>(v.radius) * static_cast<double>(v.elongation)
                              : 0.0;
                    if (d <= std::max(pixel, foot)) {
                        s.volcanic = 255;
                    }
                }
            }
        }
    });
    // The pixel of the center of every volcano, whatever its size: each shows on any map.
    const auto rows = static_cast<std::int64_t>(frame.height() + 2);
    for (const VolcanoSite& v : volcanoes.sites()) {
        const std::int64_t i = frame.columnAt(v.x) + 1;
        const std::int64_t j = frame.rowAt(v.z) + 1;
        if (i < 0 || j < 0 || i >= static_cast<std::int64_t>(columns) || j >= rows) {
            continue;
        }
        MapSample& s = out[static_cast<std::size_t>(j) * columns + static_cast<std::size_t>(i)];
        s.volcano = static_cast<std::uint8_t>(static_cast<int>(v.form) + 1);
        s.volcanic = 255;
    }
}

} // namespace terrain

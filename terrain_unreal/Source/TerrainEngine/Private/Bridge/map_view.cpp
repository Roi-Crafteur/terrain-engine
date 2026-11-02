#include "map_view.hpp"

#include "terrain/det_math.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace terrain::unreal {

namespace {

struct Turn {
    double sine = 0.0;
    double cosine = 1.0;
};

Turn turnOf(double headingDegrees) {
    const dmath::SineCosine sc = dmath::sinCosTurns(headingDegrees / 360.0);
    return {sc.sine, sc.cosine};
}

// The offset in the world (meters) of an offset on the screen from the center of the view
// (pixels): screen up is the heading of the view.
WorldPoint worldOffset(const MapView& view, double u, double v) {
    const Turn t = turnOf(view.heading);
    return {(u * t.cosine - v * t.sine) * view.metersPerPixel,
            (u * t.sine + v * t.cosine) * view.metersPerPixel};
}

WorldPoint wrapped(WorldPoint p, const WorldSize& world) {
    return {wrapCoordinate(p.x, world.x), wrapCoordinate(p.z, world.z)};
}

// The tiles of one axis covering [low, high] of the unwrapped axis: for each image k of the world
// (period) it meets, the tiles of side meters of [0, count) from its start -period / 2 + k period.
struct AxisTile {
    double start = 0.0; // unwrapped meters
    std::int64_t index = 0;
};

std::vector<AxisTile> axisTiles(double low, double high, double period, double side,
                                std::int64_t count) {
    std::vector<AxisTile> out;
    const auto firstImage = static_cast<std::int64_t>(std::floor((low + 0.5 * period) / period));
    const auto lastImage = static_cast<std::int64_t>(std::floor((high + 0.5 * period) / period));
    for (std::int64_t k = firstImage; k <= lastImage; ++k) {
        const double origin = -0.5 * period + static_cast<double>(k) * period;
        const double from = std::max(low, origin) - origin;
        const double to = std::min(high, origin + period) - origin;
        const auto first = std::clamp<std::int64_t>(
            static_cast<std::int64_t>(std::floor(from / side)), 0, count - 1);
        const auto last = std::clamp<std::int64_t>(static_cast<std::int64_t>(std::floor(to / side)),
                                                   0, count - 1);
        for (std::int64_t t = first; t <= last; ++t) {
            out.push_back({origin + static_cast<double>(t) * side, t});
        }
    }
    return out;
}

} // namespace

WorldPoint screenToWorld(const MapView& view, ScreenPoint pixel, const WorldSize& world) {
    const WorldPoint d = worldOffset(view, pixel.x - 0.5 * view.width, pixel.y - 0.5 * view.height);
    return wrapped({view.center.x + d.x, view.center.z + d.z}, world);
}

ScreenPoint worldToScreen(const MapView& view, WorldPoint point, const WorldSize& world) {
    const double dx = wrapCoordinate(point.x - view.center.x, world.x);
    const double dz = wrapCoordinate(point.z - view.center.z, world.z);
    const Turn t = turnOf(view.heading);
    return {0.5 * view.width + (dx * t.cosine + dz * t.sine) / view.metersPerPixel,
            0.5 * view.height + (-dx * t.sine + dz * t.cosine) / view.metersPerPixel};
}

MapView zoomAt(const MapView& view, ScreenPoint pixel, double factor, const WorldSize& world) {
    const WorldPoint under = screenToWorld(view, pixel, world);
    MapView zoomed = view;
    zoomed.metersPerPixel =
        std::clamp(view.metersPerPixel / factor, kMapMinMetersPerPixel, kMapMaxMetersPerPixel);
    const WorldPoint d =
        worldOffset(zoomed, pixel.x - 0.5 * view.width, pixel.y - 0.5 * view.height);
    zoomed.center = wrapped({under.x - d.x, under.z - d.z}, world);
    return zoomed;
}

MapView panBy(const MapView& view, double dx, double dy, const WorldSize& world) {
    MapView moved = view;
    moved.center = screenToWorld(view, {0.5 * view.width - dx, 0.5 * view.height - dy}, world);
    return moved;
}

int viewLevel(const MapView& view) {
    return mapLevelFor(view.metersPerPixel);
}

std::vector<MapTileDraw> tilesInView(const MapView& view, const WorldSize& world) {
    const int level = viewLevel(view);
    const double side = mapLevelMeters(level) * kMapTilePixels;
    // A turning view may show the whole disc around its center.
    const bool turns = std::fmod(view.heading, 360.0) != 0.0;
    const double reach = 0.5 * std::sqrt(view.width * view.width + view.height * view.height);
    const double halfX = (turns ? reach : 0.5 * view.width) * view.metersPerPixel;
    const double halfZ = (turns ? reach : 0.5 * view.height) * view.metersPerPixel;
    const std::vector<AxisTile> xs = axisTiles(view.center.x - halfX, view.center.x + halfX,
                                               world.x, side, mapTileColumns(level, world));
    const std::vector<AxisTile> zs = axisTiles(view.center.z - halfZ, view.center.z + halfZ,
                                               world.z, side, mapTileRows(level, world));
    std::vector<std::pair<double, MapTileDraw>> found;
    for (const AxisTile& z : zs) {
        for (const AxisTile& x : xs) {
            MapTileDraw d;
            d.key = {level, x.index, z.index};
            d.left = 0.5 * view.width + (x.start - view.center.x) / view.metersPerPixel;
            d.top = 0.5 * view.height + (z.start - view.center.z) / view.metersPerPixel;
            d.side = side / view.metersPerPixel;
            const double cx = d.left + 0.5 * d.side - 0.5 * view.width;
            const double cz = d.top + 0.5 * d.side - 0.5 * view.height;
            found.emplace_back(cx * cx + cz * cz, d);
        }
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        return std::tie(a.first, a.second.top, a.second.left) <
               std::tie(b.first, b.second.top, b.second.left);
    });
    std::vector<MapTileDraw> out;
    out.reserve(found.size());
    for (const auto& f : found) {
        out.push_back(f.second);
    }
    return out;
}

MapView minimapView(WorldPoint player, double heading, bool turns, double metersPerPixel,
                    double side, const WorldSize& world) {
    MapView view;
    view.center = wrapped(player, world);
    view.metersPerPixel = std::clamp(metersPerPixel, kMapMinMetersPerPixel, kMapMaxMetersPerPixel);
    view.heading = turns ? heading : 0.0;
    view.width = side;
    view.height = side;
    return view;
}

} // namespace terrain::unreal

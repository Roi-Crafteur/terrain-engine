#pragma once

// Pure C++ layer of the Unreal plugin (no Unreal header): compiled by Unreal with the plugin and
// by CMake for its tests (tests/unreal_map_*_test.cpp).
//
// The views of the dev map and of the minimap (agent 35): which point of the world lies under
// which pixel of the screen, and which tiles of the map pyramid (terrain/map/map_frame.hpp) cover
// a view. The widgets of Unreal only draw what this gives them.

#include "terrain/map/map_frame.hpp"
#include "terrain/world_size.hpp"

#include <vector>

namespace terrain::unreal {

// The closest and the farthest a view zooms: a quarter of a meter per screen pixel, and the whole
// default world on about 900 pixels.
inline constexpr double kMapMinMetersPerPixel = 0.25;
inline constexpr double kMapMaxMetersPerPixel = 65536.0;

// A pixel of the screen from the top left corner of the view, y down.
struct ScreenPoint {
    double x = 0.0;
    double y = 0.0;

    friend bool operator==(const ScreenPoint&, const ScreenPoint&) = default;
};

// A point of the core world (meters, x east, z south).
struct WorldPoint {
    double x = 0.0;
    double z = 0.0;

    friend bool operator==(const WorldPoint&, const WorldPoint&) = default;
};

// A view of the map: the point of the world at the center of the view (canonical), the meters per
// screen pixel, the heading shown up (degrees clockwise from the north, -z: 0 shows the north up,
// a minimap turning with the player shows its heading up) and the size of the view in pixels.
struct MapView {
    WorldPoint center;
    double metersPerPixel = 64.0;
    double heading = 0.0;
    double width = 256.0;
    double height = 256.0;

    friend bool operator==(const MapView&, const MapView&) = default;
};

// The point of the world under a pixel of the view (canonical).
[[nodiscard]] WorldPoint screenToWorld(const MapView& view, ScreenPoint pixel,
                                       const WorldSize& world = {});
// The pixel of the view over the image of a point of the world nearest to its center.
[[nodiscard]] ScreenPoint worldToScreen(const MapView& view, WorldPoint point,
                                        const WorldSize& world = {});

// The view zoomed by factor (above 1: closer) around a pixel, the point of the world under it
// staying under it; the spacing kept within [kMapMinMetersPerPixel, kMapMaxMetersPerPixel].
[[nodiscard]] MapView zoomAt(const MapView& view, ScreenPoint pixel, double factor,
                             const WorldSize& world = {});
// The view dragged by (dx, dy) pixels: the world follows the mouse.
[[nodiscard]] MapView panBy(const MapView& view, double dx, double dy, const WorldSize& world = {});

// The level of the tiles a view draws: the finest whose pixels are at least as large as its own
// (mapLevelFor), so that a tile is never drawn smaller than twice its size.
[[nodiscard]] int viewLevel(const MapView& view);

// A tile to draw: its key (wrapped: the key of the cache) and the place of its top left corner
// relative to the top left corner of the view, with its side, in screen pixels, before the view
// turns about its center (heading).
struct MapTileDraw {
    MapTileKey key;
    double left = 0.0;
    double top = 0.0;
    double side = 0.0;

    friend bool operator==(const MapTileDraw&, const MapTileDraw&) = default;
};

// The tiles of viewLevel covering the view (the whole disc around its center that a turning view
// may show), each image of the wrapped world in its place, nearest to the center first: the
// order to ask for them in. A tile appears once per image it is drawn at.
[[nodiscard]] std::vector<MapTileDraw> tilesInView(const MapView& view,
                                                   const WorldSize& world = {});

// The view of a minimap of side pixels centered on the player at (x, z), metersPerPixel apart,
// its heading up when it turns with the player, the north up otherwise.
[[nodiscard]] MapView minimapView(WorldPoint player, double heading, bool turns,
                                  double metersPerPixel, double side, const WorldSize& world = {});

} // namespace terrain::unreal

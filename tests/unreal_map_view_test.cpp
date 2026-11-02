// The views of the dev map and of the minimap (Bridge/map_view.hpp): screen <-> world, the zoom
// around the cursor, the drag, the tiles covering a view without a hole, across the seams of the
// wrapped world and turned with the player.
#include "map_view.hpp"

#include "test_assert.hpp"

#include <cmath>
#include <set>
#include <vector>

using namespace terrain;
using namespace terrain::unreal;

namespace {

const WorldSize kWorld{};

bool near(double a, double b, double tolerance) {
    return std::abs(a - b) <= tolerance;
}

bool nearPoint(WorldPoint a, WorldPoint b, double tolerance) {
    return near(wrapCoordinate(a.x - b.x, kWorld.x), 0.0, tolerance) &&
           near(wrapCoordinate(a.z - b.z, kWorld.z), 0.0, tolerance);
}

void testScreenWorld() {
    MapView view;
    view.center = {-6185104.0, 155924.0};
    view.metersPerPixel = 2.0;
    view.width = 800.0;
    view.height = 600.0;
    TEST_CHECK(screenToWorld(view, {400.0, 300.0}) == view.center);
    // North up: right is east (+x), down is south (+z).
    TEST_CHECK(nearPoint(screenToWorld(view, {500.0, 300.0}), {-6184904.0, 155924.0}, 1e-6));
    TEST_CHECK(nearPoint(screenToWorld(view, {400.0, 350.0}), {-6185104.0, 156024.0}, 1e-6));
    // Turned to the east (heading 90): up is east, right is south.
    view.heading = 90.0;
    TEST_CHECK(nearPoint(screenToWorld(view, {400.0, 200.0}), {-6184904.0, 155924.0}, 1e-6));
    TEST_CHECK(nearPoint(screenToWorld(view, {500.0, 300.0}), {-6185104.0, 156124.0}, 1e-6));
    // Back and forth, any heading, across the seam.
    bool round = true;
    for (const double heading : {0.0, 37.0, 227.0}) {
        view.heading = heading;
        view.center = {0.5 * kWorld.x - 100.0, -0.5 * kWorld.z + 50.0};
        for (const ScreenPoint p : {ScreenPoint{0.0, 0.0}, ScreenPoint{799.0, 599.0},
                                    ScreenPoint{123.5, 456.25}}) {
            const ScreenPoint back = worldToScreen(view, screenToWorld(view, p));
            round = round && near(back.x, p.x, 1e-6) && near(back.y, p.y, 1e-6);
        }
    }
    TEST_CHECK(round);
}

void testZoomAndPan() {
    MapView view;
    view.center = {1430074.0, 2052007.0};
    view.metersPerPixel = 1024.0;
    view.width = 1000.0;
    view.height = 700.0;
    const ScreenPoint cursor{180.0, 620.0};
    const WorldPoint under = screenToWorld(view, cursor);
    MapView zoomed = view;
    for (int i = 0; i < 12; ++i) {
        zoomed = zoomAt(zoomed, cursor, 2.0);
    }
    TEST_CHECK(nearPoint(screenToWorld(zoomed, cursor), under, 1e-3));
    TEST_CHECK(zoomed.metersPerPixel == kMapMinMetersPerPixel);
    const MapView far = zoomAt(view, cursor, 1.0e-9);
    TEST_CHECK(far.metersPerPixel == kMapMaxMetersPerPixel);
    // Dragging by (dx, dy): the point under the mouse follows it.
    const MapView moved = panBy(view, 40.0, -25.0);
    TEST_CHECK(nearPoint(screenToWorld(moved, {cursor.x + 40.0, cursor.y - 25.0}), under, 1e-6));
}

// Every pixel of the view lies in a drawn tile, every tile drawn overlaps the view.
bool covers(const MapView& view, const std::vector<MapTileDraw>& tiles) {
    for (double y = 0.5; y < view.height; y += 37.0) {
        for (double x = 0.5; x < view.width; x += 41.0) {
            bool in = false;
            for (const MapTileDraw& t : tiles) {
                in = in || (x >= t.left && x < t.left + t.side && y >= t.top && y < t.top + t.side);
            }
            if (!in) {
                return false;
            }
        }
    }
    return true;
}

void testTiles() {
    MapView view;
    view.width = 1920.0;
    view.height = 1080.0;
    for (const double mpp : {0.25, 3.0, 64.0, 1000.0, 31266.0, 65536.0}) {
        view.metersPerPixel = mpp;
        for (const WorldPoint c : {WorldPoint{0.0, 0.0}, WorldPoint{0.5 * kWorld.x - 10.0, 0.0},
                                   WorldPoint{-0.5 * kWorld.x, -0.5 * kWorld.z}}) {
            view.center = c;
            const std::vector<MapTileDraw> tiles = tilesInView(view);
            bool valid = covers(view, tiles);
            for (const MapTileDraw& t : tiles) {
                valid = valid && t.key.level == viewLevel(view) && t.key == wrapTile(t.key) &&
                        near(t.side, mapLevelMeters(t.key.level) * kMapTilePixels / mpp, 1e-9);
            }
            TEST_CHECK(valid);
        }
    }
    // Nearest to the center first.
    view.metersPerPixel = 4.0;
    view.center = {100.0, 100.0};
    const std::vector<MapTileDraw> tiles = tilesInView(view);
    TEST_CHECK(tiles.front().key == mapTileAt(2, 100.0, 100.0));
    // A minimap turning with the player covers its disc.
    const MapView mini = minimapView({0.5 * kWorld.x - 5.0, 0.0}, 33.0, true, 4.0, 256.0);
    TEST_CHECK(mini.heading == 33.0 && mini.width == 256.0);
    TEST_CHECK(minimapView({0.0, 0.0}, 33.0, false, 4.0, 256.0).heading == 0.0);
    const std::vector<MapTileDraw> disc = tilesInView(mini);
    std::set<MapTileKey> keys;
    for (const MapTileDraw& t : disc) {
        keys.insert(t.key);
    }
    TEST_CHECK(keys.size() == disc.size() && !disc.empty());
    // Every corner of the turned square falls in a drawn tile.
    bool corners = true;
    for (const ScreenPoint p : {ScreenPoint{0, 0}, ScreenPoint{256, 0}, ScreenPoint{0, 256},
                                ScreenPoint{256, 256}}) {
        const WorldPoint w = screenToWorld(mini, p);
        corners = corners && keys.count(mapTileAt(viewLevel(mini), w.x, w.z)) == 1;
    }
    TEST_CHECK(corners);
}

} // namespace

int main() {
    testScreenWorld();
    testZoomAndPan();
    testTiles();
    return test::result();
}

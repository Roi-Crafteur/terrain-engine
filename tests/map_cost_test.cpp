// The cost of the dev map (map_render.hpp, map_source.hpp): the cap of samples per image, and the
// guard of the hydrology: a tile of 128 m per pixel or more never queries the Hydrology (its
// courses cost too much over large boxes), the counter of the source and the courses computed by
// the Hydrology both say so. The times of tiles at several spacings are printed, not checked.
#include "map_world_support.hpp"
#include "test_assert.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace terrain;

namespace {

double secondsOf(const std::chrono::steady_clock::time_point& start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// An image of 2,048 x 2,048 pixels at 1 m per pixel: above the cap, sampled every 3 pixels.
void testCap() {
    const map_world::DryWorld& w = map_world::dry();
    const MapFrame f = map_world::frameAround(-3985.0, -165.0, 1.0, 2048);
    const auto start = std::chrono::steady_clock::now();
    const MapImage image = renderMap(w.source, f, defaultMapLayers(), defaultMapLayerNames(),
                                     {map_world::threads()});
    std::printf("2048 x 2048 at 1 m/px: stride %d, %zu samples of the relief, %.2f s\n",
                image.stride, image.stats.reliefSamples, secondsOf(start));
    TEST_CHECK(image.stride == 3);
    TEST_CHECK(image.stats.samples <= kMapMaxSamples && image.stats.reliefSamples <= kMapMaxSamples);
    TEST_CHECK(image.width == 2048 && image.height == 2048);
    // A tile is never reduced.
    const MapImage tile = renderMap(w.source, mapTileFrame(mapTileAt(0, 0.0, 0.0)),
                                    defaultMapLayers(), defaultMapLayerNames());
    TEST_CHECK(tile.stride == 1 && tile.stats.samples == 258u * 258u);
}

// Tiles of 128 m per pixel and more around the rivers of the small world, over a hydrology that
// has computed nothing yet: no query, no course. Then one of 64 m per pixel does query it.
void testHydrologyGuard() {
    const map_world::FallPlace fall = map_world::firstFall();
    const map_world::WetWorld w;
    TEST_CHECK(w.hydrology->coursesComputed() == 0);
    const MapLayers layers = defaultMapLayers();
    const std::vector<std::string> all = layers.names();
    std::size_t calls = 0;
    for (const int level : {7, 8, 10, 11, 13, 15}) {
        const MapTileKey key = mapTileAt(level, fall.x, fall.z, golden::kSmallPlanet);
        const MapImage image =
            renderMap(w.source, mapTileFrame(key, golden::kSmallPlanet), layers, all, {2});
        calls += image.stats.hydrologyCalls;
    }
    TEST_CHECK(calls == 0);
    TEST_CHECK(w.hydrology->coursesComputed() == 0);
    const MapImage fine = renderMap(
        w.source, mapTileFrame(mapTileAt(6, fall.x, fall.z, golden::kSmallPlanet), golden::kSmallPlanet),
        layers, all, {2});
    TEST_CHECK(fine.stats.hydrologyCalls > 0 && w.hydrology->coursesComputed() > 0);
}

// The time of a tile at several spacings, one thread (printed for the reports).
void printTileTimes() {
    const map_world::FallPlace fall = map_world::firstFall();
    const map_world::WetWorld w;
    const MapLayers layers = defaultMapLayers();
    for (const int level : {0, 2, 6, 7, 11, 13}) {
        const MapTileKey key = mapTileAt(level, fall.x, fall.z, golden::kSmallPlanet);
        const auto start = std::chrono::steady_clock::now();
        const MapImage image = renderMap(w.source, mapTileFrame(key, golden::kSmallPlanet), layers,
                                         layers.names(), {1});
        std::printf("tile of %5.0f m/px by the first fall of the small world: %.3f s (%zu queries "
                    "of the hydrology)\n",
                    mapLevelMeters(level), secondsOf(start), image.stats.hydrologyCalls);
    }
    std::printf("courses of the hydrology: %zu, %.2f MB\n", w.hydrology->coursesComputed(),
                static_cast<double>(w.hydrology->courseBytes()) / 1.0e6);
}

} // namespace

int main() {
    testCap();
    testHydrologyGuard();
    printTileTimes();
    return test::result();
}

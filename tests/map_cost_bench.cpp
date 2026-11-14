// The cost of the dev map (map_render.hpp, map_source.hpp), run by hand (not a test; it asserts
// nothing): map_cost_bench, built in Release. It prints the time of an image of 2,048 x 2,048
// pixels at 1 m per pixel (above the cap of samples, so sampled every 3 pixels) on every hardware
// thread, and the time of a tile at several spacings by the first fall of the small world, on one
// thread, over a hydrology that has computed nothing yet, with the courses it computed and their
// memory. map_cost_test asserts the cap and the guard of the hydrology.
#include "map_world_support.hpp"

#include <chrono>
#include <cstdio>

using namespace terrain;

namespace {

double secondsOf(const std::chrono::steady_clock::time_point& start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void printImageTime() {
    const map_world::DryWorld& w = map_world::dry();
    const MapFrame f = map_world::frameAround(-3985.0, -165.0, 1.0, 2048);
    const auto start = std::chrono::steady_clock::now();
    const MapImage image = renderMap(w.source, f, defaultMapLayers(), defaultMapLayerNames(),
                                     {map_world::threads()});
    std::printf("2048 x 2048 at 1 m/px: stride %d, %zu samples of the relief, %.2f s\n",
                image.stride, image.stats.reliefSamples, secondsOf(start));
}

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
    printImageTime();
    printTileTimes();
    return 0;
}

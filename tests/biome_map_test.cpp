// The biomes of the dev map over the real planet (agent 49; the fixture coast_planet, as Unreal
// loads it): the sand desert follows the seas of dunes on the map too, and the map still never
// asks the hydrology for a course from 64 m per pixel (map_cost_test, its guard): over an erg, at
// 200 m per pixel (the relief alone) and 4 km per pixel (the cells of the planet level), the
// pixels of the sand desert are there and no course of a river is computed.
#include "terrain/generation/biome.hpp"
#include "terrain/generation/water/hydrology.hpp"
#include "terrain/map/map_render.hpp"
#include "terrain/map/map_source.hpp"

#include "coast_planet_support.hpp"
#include "test_assert.hpp"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace {

// The heart of a sand desert (terrain_biome_places: sand desert all around).
constexpr double kErgX = 14'264'512.0;
constexpr double kErgZ = -4'818'112.0;

// The pixels of the sand desert of a square frame around the erg, step meters per pixel.
int sandPixels(const terrain::MapWorldSource& source, double step, int size,
               terrain::MapSourceKind& kind) {
    const double side = step * size;
    const terrain::MapFrame frame({kErgX - 0.5 * side, kErgZ - 0.5 * side, side, side}, size,
                                  size, coast_planet::reliefTiles().world());
    std::vector<terrain::MapSample> out(terrain::mapGridSize(frame));
    terrain::MapSampleStats stats;
    const auto start = std::chrono::steady_clock::now();
    source.sample(frame, out, 4, stats, nullptr);
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    int sand = 0;
    for (int j = 0; j < size; ++j) {
        for (int i = 0; i < size; ++i) {
            sand += out[terrain::mapGridIndex(frame, i, j)].biome == terrain::Biome::SandDesert;
        }
    }
    kind = out[terrain::mapGridIndex(frame, 0, 0)].source;
    std::printf("%.0f m per pixel: %d of %d pixels of sand desert, %.2f s\n", step, sand,
                size * size, seconds);
    return sand;
}

} // namespace

int main() {
    const terrain::TerrainTiles& tiles = coast_planet::reliefTiles();
    const terrain::MapWorldSource source(tiles);
    terrain::MapSourceKind kind{};
    TEST_CHECK(sandPixels(source, 200.0, 160, kind) > 1000);
    TEST_CHECK(kind == terrain::MapSourceKind::Relief);
    TEST_CHECK(sandPixels(source, 4096.0, 128, kind) > 100);
    TEST_CHECK(kind == terrain::MapSourceKind::PlanetCells);
    const std::size_t courses = coast_planet::hydrology()->coursesComputed();
    std::printf("courses of the hydrology computed: %zu\n", courses);
    TEST_CHECK(courses == 0);
    return test::result();
}

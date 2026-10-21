// Control images of the planet level: sizes, colors of known cells, files written.
#include "terrain/io/planet_png.hpp"

#include "planet_golden_support.hpp"
#include "test_assert.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

using terrain::Image;
using terrain::PlanetLevel;

struct Pixel {
    int r;
    int g;
    int b;
};

Pixel pixel(const Image& image, int x, int y) {
    const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                            static_cast<std::size_t>(x)) *
                           3;
    return {image.pixels[at], image.pixels[at + 1], image.pixels[at + 2]};
}

const PlanetLevel& level() {
    static const PlanetLevel planet(terrain::kDefaultWorldSeed, golden::smallPlanetaryParams(),
                                    golden::smallPlanetParams(2), golden::kSmallPlanet);
    return planet;
}

// A pixel at scale 1 whose cell is sea, and one whose cell is dry land away from lakes.
void findCells(int& seaX, int& seaY, int& landX, int& landY) {
    const terrain::PlanetGrid& grid = level().grid();
    for (std::size_t c = 0; c < grid.cells(); ++c) {
        if (level().cells().ocean(c)) {
            seaX = grid.column(c);
            seaY = grid.row(c);
        } else if (level().cells().lakeAt(c) < 0 && level().elevation(c) > 300.0f) {
            landX = grid.column(c);
            landY = grid.row(c);
        }
    }
}

void testScale() {
    TEST_CHECK(terrain::planetImageScale(terrain::makePlanetGrid({}, 8192.0), 4096) == 2);
    TEST_CHECK(terrain::planetImageScale(level().grid(), 4096) == 1);
    TEST_CHECK(terrain::planetImageScale(level().grid(), 100) == 3);
    const Image small = terrain::renderPlanetRelief(level(), 3);
    TEST_CHECK(small.width == 86 && small.height == 43 && small.channels == 3);
    TEST_CHECK(small.pixels.size() == 86u * 43u * 3u);
}

void testColors() {
    int seaX = 0, seaY = 0, landX = 0, landY = 0;
    findCells(seaX, seaY, landX, landY);
    const Image relief = terrain::renderPlanetRelief(level(), 1);
    const Pixel sea = pixel(relief, seaX, seaY);
    const Pixel land = pixel(relief, landX, landY);
    TEST_CHECK(sea.b > sea.r && sea.b > sea.g);
    TEST_CHECK(!(land.b > land.r && land.b > land.g));
    // Cold poles, warm equator.
    const Image heat = terrain::renderPlanetTemperature(level(), 1);
    const Pixel pole = pixel(heat, 10, 0);
    const Pixel equator = pixel(heat, 10, level().grid().nz / 2);
    TEST_CHECK(pole.b > pole.r && equator.r > equator.b);
    const Image rain = terrain::renderPlanetPrecipitation(level(), 1);
    TEST_CHECK(rain.width == level().grid().nx && rain.height == level().grid().nz);
    // One color per basin, the sea in dark blue.
    const std::vector<std::int32_t> basins = level().basins();
    const Image basin = terrain::renderPlanetBasins(level(), basins, 1);
    TEST_CHECK(pixel(basin, seaX, seaY).r == 10 && pixel(basin, seaX, seaY).b == 60);
    const terrain::PlanetGrid& grid = level().grid();
    bool consistent = true;
    for (std::size_t c = 1; c < grid.cells(); ++c) {
        if (basins[c] >= 0 && basins[c] == basins[c - 1] && grid.column(c) > 0) {
            const Pixel a = pixel(basin, grid.column(c), grid.row(c));
            const Pixel b = pixel(basin, grid.column(c - 1), grid.row(c - 1));
            consistent = consistent && a.r == b.r && a.g == b.g && a.b == b.b;
        }
    }
    TEST_CHECK(consistent);
}

void testRiversAndLakes() {
    const terrain::PlanetGrid& grid = level().grid();
    const Image lakes = terrain::renderPlanetLakes(level(), 1);
    bool painted = !level().cells().lakeCells.empty();
    for (const terrain::PlanetCells::LakeCell& cell : level().cells().lakeCells) {
        const Pixel p = pixel(lakes, grid.column(cell.cell), grid.row(cell.cell));
        painted = painted && p.b >= 165 && p.r <= 215;
    }
    TEST_CHECK(painted);
    const Image rivers = terrain::renderPlanetRivers(level(), 1);
    TEST_CHECK(!level().rivers().segments.empty());
    const terrain::RiverVertex& v = level().rivers().segments[0].vertices[0];
    const Pixel p = pixel(rivers, grid.column(v.cell), grid.row(v.cell));
    TEST_CHECK(p.b > p.r && p.b >= 200);
}

void testExport() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "terrain_planet_png_test";
    std::filesystem::remove_all(dir);
    TEST_CHECK(terrain::exportPlanetPngs(level(), dir, 100));
    const std::vector<std::uint8_t> expected =
        terrain::encodePng(terrain::renderPlanetRelief(level(), 3));
    std::ifstream file(dir / "planet_relief.png", std::ios::binary);
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
    TEST_CHECK(bytes == expected);
    for (const char* name : {"planet_precipitation.png", "planet_temperature.png",
                             "planet_basins.png", "planet_rivers.png", "planet_lakes.png"}) {
        TEST_CHECK(std::filesystem::file_size(dir / name) > 0);
    }
    std::filesystem::remove_all(dir);
}

} // namespace

int main() {
    testScale();
    testColors();
    testRiversAndLakes();
    testExport();
    return test::result();
}

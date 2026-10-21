#include "terrain/io/planet_png.hpp"

#include "planet_png_draw.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <system_error>

namespace terrain {

namespace {

using planet_png::ColorStop;
using planet_png::Rgb;

constexpr std::array<ColorStop, 7> kLand = {{
    {0.0f, {95.0f, 150.0f, 75.0f}},
    {250.0f, {135.0f, 170.0f, 95.0f}},
    {600.0f, {170.0f, 160.0f, 105.0f}},
    {1200.0f, {150.0f, 125.0f, 95.0f}},
    {1800.0f, {125.0f, 115.0f, 108.0f}},
    {2400.0f, {220.0f, 220.0f, 220.0f}},
    {3000.0f, {255.0f, 255.0f, 255.0f}},
}};
constexpr std::array<ColorStop, 3> kSea = {{
    {-4000.0f, {15.0f, 35.0f, 95.0f}},
    {-1000.0f, {40.0f, 90.0f, 160.0f}},
    {0.0f, {120.0f, 180.0f, 225.0f}},
}};
constexpr std::array<ColorStop, 7> kRain = {{
    {0.0f, {225.0f, 205.0f, 150.0f}},
    {250.0f, {215.0f, 210.0f, 110.0f}},
    {500.0f, {150.0f, 195.0f, 90.0f}},
    {1000.0f, {60.0f, 160.0f, 80.0f}},
    {2000.0f, {40.0f, 150.0f, 160.0f}},
    {3500.0f, {40.0f, 80.0f, 190.0f}},
    {5000.0f, {120.0f, 50.0f, 170.0f}},
}};
constexpr std::array<ColorStop, 8> kHeat = {{
    {-40.0f, {70.0f, 20.0f, 100.0f}},
    {-20.0f, {50.0f, 60.0f, 190.0f}},
    {-10.0f, {90.0f, 160.0f, 230.0f}},
    {0.0f, {240.0f, 240.0f, 240.0f}},
    {10.0f, {240.0f, 220.0f, 120.0f}},
    {20.0f, {240.0f, 150.0f, 50.0f}},
    {30.0f, {200.0f, 40.0f, 30.0f}},
    {35.0f, {140.0f, 0.0f, 20.0f}},
}};
constexpr Rgb kIce = {200.0f, 225.0f, 245.0f};
constexpr float kSeaShade = 0.55f;

// Mean of value(cell) and share of ocean cells over each pixel, as an image.
template <class Value, class Color>
Image renderMean(const PlanetLevel& level, int scale, Value value, Color color) {
    const PlanetGrid& grid = level.grid();
    Image image = planet_png::blankImage(grid, scale);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const planet_png::Block b = planet_png::block(grid, scale, x, y);
            double sum = 0.0;
            int sea = 0;
            int count = 0;
            for (int j = b.j0; j < b.j1; ++j) {
                for (int i = b.i0; i < b.i1; ++i) {
                    const std::size_t c = grid.index(i, j);
                    sum += value(c);
                    sea += level.cells().ocean(c) ? 1 : 0;
                    ++count;
                }
            }
            planet_png::put(image, x, y,
                            color(static_cast<float>(sum / count), 2 * sea > count,
                                  planet_png::middleCell(grid, scale, x, y)));
        }
    }
    return image;
}

} // namespace

int planetImageScale(const PlanetGrid& grid, int maxWidth) {
    return std::max(1, (grid.nx + std::max(maxWidth, 1) - 1) / std::max(maxWidth, 1));
}

Image renderPlanetRelief(const PlanetLevel& level, int scale) {
    const PlanetGrid& grid = level.grid();
    const std::vector<float> elevation = planet_png::meanElevation(level, scale);
    Image image = planet_png::blankImage(grid, scale);
    const double pixel = grid.cellSize * scale;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const float h = elevation[static_cast<std::size_t>(y * image.width + x)];
            Rgb color = planet_png::ramp(kSea, h);
            if (!level.cells().ocean(planet_png::middleCell(grid, scale, x, y))) {
                color = planet_png::scaled(
                    planet_png::ramp(kLand, std::max(h, 0.0f)),
                    planet_png::shading(elevation, image.width, image.height, pixel, x, y));
            }
            planet_png::put(image, x, y, color);
        }
    }
    return image;
}

Image renderPlanetPrecipitation(const PlanetLevel& level, int scale) {
    return renderMean(
        level, scale, [&](std::size_t c) { return level.precipitation(c); },
        [](float p, bool sea, std::size_t) {
            const Rgb color = planet_png::ramp(kRain, p);
            return sea ? planet_png::scaled(color, kSeaShade) : color;
        });
}

Image renderPlanetTemperature(const PlanetLevel& level, int scale) {
    const float ice = level.params().climate.iceTemperature;
    return renderMean(
        level, scale, [&](std::size_t c) { return level.temperature(c); },
        [ice](float t, bool sea, std::size_t) {
            if (sea) {
                return planet_png::scaled(planet_png::ramp(kHeat, t), kSeaShade);
            }
            return t < ice ? planet_png::mix(planet_png::ramp(kHeat, t), kIce, 0.6f)
                           : planet_png::ramp(kHeat, t);
        });
}

Image renderPlanetBasins(const PlanetLevel& level, const std::vector<std::int32_t>& basins,
                         int scale) {
    const PlanetGrid& grid = level.grid();
    Image image = planet_png::blankImage(grid, scale);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const std::int32_t basin = basins[planet_png::middleCell(grid, scale, x, y)];
            Rgb color{10.0f, 20.0f, 60.0f};
            if (basin >= 0) {
                const std::uint64_t bits =
                    noise::deriveSeed(0x6261736Eu, static_cast<std::uint64_t>(basin));
                color = {static_cast<float>(60 + (bits & 0xBF)),
                         static_cast<float>(60 + ((bits >> 8) & 0xBF)),
                         static_cast<float>(60 + ((bits >> 16) & 0xBF))};
            }
            planet_png::put(image, x, y, color);
        }
    }
    return image;
}

bool exportPlanetPngs(const PlanetLevel& level, const std::filesystem::path& directory,
                      int maxWidth) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return false;
    }
    const int scale = planetImageScale(level.grid(), maxWidth);
    bool written = writePng(directory / "planet_relief.png", renderPlanetRelief(level, scale));
    written =
        writePng(directory / "planet_precipitation.png", renderPlanetPrecipitation(level, scale)) &&
        written;
    written =
        writePng(directory / "planet_temperature.png", renderPlanetTemperature(level, scale)) &&
        written;
    written = writePng(directory / "planet_basins.png",
                       renderPlanetBasins(level, level.basins(), scale)) &&
              written;
    written =
        writePng(directory / "planet_rivers.png", renderPlanetRivers(level, scale)) && written;
    written = writePng(directory / "planet_lakes.png", renderPlanetLakes(level, scale)) && written;
    return written;
}

} // namespace terrain

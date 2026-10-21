#pragma once

// Internal to the planet images: colors, pixel blocks and drawing on the wrapped world.

#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/io/png.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace terrain::planet_png {

struct Rgb {
    float r;
    float g;
    float b;
};

struct ColorStop {
    float value;
    Rgb color;
};

// Linear between the stops (sorted by value), clamped to the first and last colors.
[[nodiscard]] Rgb ramp(std::span<const ColorStop> stops, float value);
[[nodiscard]] Rgb scaled(Rgb color, float factor);
[[nodiscard]] Rgb mix(Rgb a, Rgb b, float t);

// An RGB image of the size of the grid divided by scale (rounded up).
[[nodiscard]] Image blankImage(const PlanetGrid& grid, int scale);
void put(Image& image, int x, int y, Rgb color);

// The cells of pixel (x, y): columns [x scale, ...) and rows [y scale, ...) within the grid.
struct Block {
    int i0;
    int j0;
    int i1;
    int j1;
};
[[nodiscard]] Block block(const PlanetGrid& grid, int scale, int x, int y);
// The cell at the middle of a pixel.
[[nodiscard]] std::size_t middleCell(const PlanetGrid& grid, int scale, int x, int y);
// Mean altitude of every pixel, row by row.
[[nodiscard]] std::vector<float> meanElevation(const PlanetLevel& level, int scale);
// Lambert factor (flat ground = 1) of the pixel slopes, lit from the north-west, on the torus.
[[nodiscard]] float shading(const std::vector<float>& elevation, int width, int height,
                            double pixelMeters, int x, int y);

// The relief in grays (the sea darker), shaded: the background of the rivers and the lakes.
[[nodiscard]] Image grayRelief(const PlanetLevel& level, int scale);
// The rivers drawn over an image, width and brightness by discharge (thin: one pixel, dim).
void drawRivers(Image& image, const PlanetLevel& level, int scale, bool thin);

} // namespace terrain::planet_png

#include "planet_png_draw.hpp"

#include "terrain/det_math.hpp"
#include "terrain/io/planet_png.hpp"

#include <algorithm>
#include <cmath>

namespace terrain {

namespace planet_png {

namespace {

constexpr double kLn10 = 2.302585092994046;

int wrap(int v, int period) {
    return ((v % period) + period) % period;
}

// A disc of radius around (u, v) in pixels, across the edges of the image.
void stamp(Image& image, double u, double v, double radius, Rgb color) {
    const int reach = static_cast<int>(std::ceil(radius));
    const int cu = static_cast<int>(std::floor(u));
    const int cv = static_cast<int>(std::floor(v));
    for (int dy = -reach; dy <= reach; ++dy) {
        for (int dx = -reach; dx <= reach; ++dx) {
            if (dx * dx + dy * dy <= radius * radius + 0.25) {
                put(image, wrap(cu + dx, image.width), wrap(cv + dy, image.height), color);
            }
        }
    }
}

} // namespace

Rgb ramp(std::span<const ColorStop> stops, float value) {
    if (value <= stops.front().value) {
        return stops.front().color;
    }
    for (std::size_t k = 1; k < stops.size(); ++k) {
        if (value < stops[k].value) {
            const float t = (value - stops[k - 1].value) / (stops[k].value - stops[k - 1].value);
            return mix(stops[k - 1].color, stops[k].color, t);
        }
    }
    return stops.back().color;
}

Rgb scaled(Rgb color, float factor) {
    return {color.r * factor, color.g * factor, color.b * factor};
}

Rgb mix(Rgb a, Rgb b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

Image blankImage(const PlanetGrid& grid, int scale) {
    Image image;
    image.width = (grid.nx + scale - 1) / scale;
    image.height = (grid.nz + scale - 1) / scale;
    image.channels = 3;
    image.pixels.assign(
        static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 3, 0);
    return image;
}

void put(Image& image, int x, int y, Rgb color) {
    const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                            static_cast<std::size_t>(x)) *
                           3;
    image.pixels[at] = static_cast<std::uint8_t>(std::lround(std::clamp(color.r, 0.0f, 255.0f)));
    image.pixels[at + 1] =
        static_cast<std::uint8_t>(std::lround(std::clamp(color.g, 0.0f, 255.0f)));
    image.pixels[at + 2] =
        static_cast<std::uint8_t>(std::lround(std::clamp(color.b, 0.0f, 255.0f)));
}

Block block(const PlanetGrid& grid, int scale, int x, int y) {
    return {x * scale, y * scale, std::min(grid.nx, (x + 1) * scale),
            std::min(grid.nz, (y + 1) * scale)};
}

std::size_t middleCell(const PlanetGrid& grid, int scale, int x, int y) {
    const Block b = block(grid, scale, x, y);
    return grid.index((b.i0 + b.i1 - 1) / 2, (b.j0 + b.j1 - 1) / 2);
}

std::vector<float> meanElevation(const PlanetLevel& level, int scale) {
    const PlanetGrid& grid = level.grid();
    const int width = (grid.nx + scale - 1) / scale;
    const int height = (grid.nz + scale - 1) / scale;
    std::vector<float> mean(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Block b = block(grid, scale, x, y);
            double sum = 0.0;
            for (int j = b.j0; j < b.j1; ++j) {
                for (int i = b.i0; i < b.i1; ++i) {
                    sum += level.elevation(grid.index(i, j));
                }
            }
            mean[static_cast<std::size_t>(y * width + x)] =
                static_cast<float>(sum / ((b.i1 - b.i0) * (b.j1 - b.j0)));
        }
    }
    return mean;
}

float shading(const std::vector<float>& elevation, int width, int height, double pixelMeters, int x,
              int y) {
    const auto at = [&](int i, int j) {
        return static_cast<double>(
            elevation[static_cast<std::size_t>(wrap(j, height) * width + wrap(i, width))]);
    };
    const double slopeX = (at(x + 1, y) - at(x - 1, y)) / (2.0 * pixelMeters);
    const double slopeZ = (at(x, y + 1) - at(x, y - 1)) / (2.0 * pixelMeters);
    // Normal (-slopeX, 1, -slopeZ), light from the north-west (-X, -Z), 45 degrees high;
    // slopes exaggerated 20 times, the planet being flat at this scale.
    const double sx = 20.0 * slopeX;
    const double sz = 20.0 * slopeZ;
    const double lambert =
        (0.5 * sx + std::sqrt(0.5) + 0.5 * sz) / std::sqrt(sx * sx + 1.0 + sz * sz);
    return static_cast<float>(std::clamp(0.35 + 0.65 * lambert / std::sqrt(0.5), 0.3, 1.3));
}

Image grayRelief(const PlanetLevel& level, int scale) {
    const PlanetGrid& grid = level.grid();
    const std::vector<float> elevation = meanElevation(level, scale);
    Image image = blankImage(grid, scale);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const float h = elevation[static_cast<std::size_t>(y * image.width + x)];
            if (level.cells().ocean(middleCell(grid, scale, x, y))) {
                const float depth = std::clamp(-h / 4000.0f, 0.0f, 1.0f);
                put(image, x, y,
                    {30.0f - 15.0f * depth, 40.0f - 20.0f * depth, 60.0f - 25.0f * depth});
                continue;
            }
            const float gray =
                (80.0f + 120.0f * std::clamp(h / 3000.0f, 0.0f, 1.0f)) *
                shading(elevation, image.width, image.height, grid.cellSize * scale, x, y);
            put(image, x, y, {gray, gray, gray});
        }
    }
    return image;
}

void drawRivers(Image& image, const PlanetLevel& level, int scale, bool thin) {
    const PlanetGrid& grid = level.grid();
    const double pixel = grid.cellSize * scale;
    const double minimum = level.params().rivers.minDischarge;
    for (const RiverSegment& segment : level.rivers().segments) {
        for (std::size_t v = 0; v + 1 < segment.vertices.size(); ++v) {
            const RiverVertex& a = segment.vertices[v];
            const RiverVertex& b = segment.vertices[v + 1];
            const double size =
                std::max(0.0, dmath::log(std::max(a.discharge, 1.0f) / minimum) / kLn10);
            const double radius = thin ? 0.0 : std::min(3.0, 0.8 * size);
            const float t = static_cast<float>(std::min(1.0, size / 3.0));
            const Rgb color = thin ? Rgb{110.0f, 170.0f, 230.0f}
                                   : mix({50.0f, 110.0f, 210.0f}, {160.0f, 235.0f, 255.0f}, t);
            const double u0 = (a.x + 0.5 * grid.periodX()) / pixel;
            const double v0 = (a.z + 0.5 * grid.periodZ()) / pixel;
            const double u1 = (b.x + 0.5 * grid.periodX()) / pixel;
            const double v1 = (b.z + 0.5 * grid.periodZ()) / pixel;
            const double steps =
                std::ceil(2.0 * std::max(std::abs(u1 - u0), std::abs(v1 - v0))) + 1.0;
            for (double s = 0.0; s <= steps; s += 1.0) {
                stamp(image, u0 + (u1 - u0) * s / steps, v0 + (v1 - v0) * s / steps, radius, color);
            }
        }
    }
}

} // namespace planet_png

Image renderPlanetRivers(const PlanetLevel& level, int scale) {
    Image image = planet_png::grayRelief(level, scale);
    planet_png::drawRivers(image, level, scale, false);
    return image;
}

Image renderPlanetLakes(const PlanetLevel& level, int scale) {
    const PlanetGrid& grid = level.grid();
    Image image = planet_png::grayRelief(level, scale);
    planet_png::drawRivers(image, level, scale, true);
    for (const PlanetCells::LakeCell& cell : level.cells().lakeCells) {
        const PlanetLake& lake = level.lakes()[static_cast<std::size_t>(cell.lake)];
        const planet_png::Rgb color = lake.frozen        ? planet_png::Rgb{215.0f, 235.0f, 255.0f}
                                      : lake.overflowing ? planet_png::Rgb{40.0f, 110.0f, 225.0f}
                                                         : planet_png::Rgb{30.0f, 175.0f, 165.0f};
        planet_png::put(image, grid.column(cell.cell) / scale, grid.row(cell.cell) / scale, color);
    }
    for (const PlanetLake& lake : level.lakes()) {
        if (lake.playa) {
            const int x = grid.column(lake.pit) / scale;
            const int y = grid.row(lake.pit) / scale;
            planet_png::put(image, x, y, {235.0f, 220.0f, 175.0f});
        }
    }
    return image;
}

} // namespace terrain

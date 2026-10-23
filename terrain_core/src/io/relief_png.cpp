#include "terrain/io/relief_png.hpp"

#include "../generation/planet/planet_parallel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace terrain {

namespace {

struct Rgb {
    float r;
    float g;
    float b;
};

struct ColorStop {
    float height;
    Rgb color;
};

// Hypsometric tints from the abyssal plains to the highest summits: deep blues, the shelf in
// light blue, lowland greens, the yellows and browns of uplands and plateaus, gray rock, snow.
constexpr std::array<ColorStop, 5> kSea = {{
    {-7000.0f, {8.0f, 20.0f, 60.0f}},
    {-4000.0f, {20.0f, 50.0f, 115.0f}},
    {-1500.0f, {40.0f, 95.0f, 165.0f}},
    {-200.0f, {95.0f, 160.0f, 215.0f}},
    {0.0f, {135.0f, 195.0f, 230.0f}},
}};
constexpr std::array<ColorStop, 10> kLand = {{
    {0.0f, {80.0f, 140.0f, 72.0f}},
    {200.0f, {118.0f, 162.0f, 88.0f}},
    {500.0f, {168.0f, 178.0f, 108.0f}},
    {1000.0f, {198.0f, 170.0f, 112.0f}},
    {1600.0f, {180.0f, 140.0f, 98.0f}},
    {2400.0f, {150.0f, 115.0f, 92.0f}},
    {3200.0f, {135.0f, 120.0f, 112.0f}},
    {4200.0f, {165.0f, 160.0f, 158.0f}},
    {5200.0f, {228.0f, 228.0f, 232.0f}},
    {6500.0f, {255.0f, 255.0f, 255.0f}},
}};

template <std::size_t N>
Rgb tint(const std::array<ColorStop, N>& stops, float height) {
    if (height <= stops.front().height) {
        return stops.front().color;
    }
    if (height >= stops.back().height) {
        return stops.back().color;
    }
    std::size_t i = 0;
    while (height >= stops[i + 1].height) {
        ++i;
    }
    const ColorStop& a = stops[i];
    const ColorStop& b = stops[i + 1];
    const float t = (height - a.height) / (b.height - a.height);
    return {a.color.r + (b.color.r - a.color.r) * t, a.color.g + (b.color.g - a.color.g) * t,
            a.color.b + (b.color.b - a.color.b) * t};
}

std::uint8_t toByte(float value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 255.0f)));
}

// Neighbor index along one axis: across the edge of a wrapping grid, clamped otherwise.
int neighbor(int i, int count, bool wraps) {
    if (wraps) {
        return (i % count + count) % count;
    }
    return std::clamp(i, 0, count - 1);
}

// Lambert factor of the exaggerated ground normal, relative to flat ground, lit from the
// north-west (-X, -Z) 45 degrees high.
float shading(const ReliefGrid& grid, int i, int j, double exaggeration) {
    const int left = neighbor(i - 1, grid.columns, grid.wraps);
    const int right = neighbor(i + 1, grid.columns, grid.wraps);
    const int up = neighbor(j - 1, grid.rows, grid.wraps);
    const int down = neighbor(j + 1, grid.rows, grid.wraps);
    const double runX = (grid.wraps ? 2 : right - left) * grid.spacing;
    const double runZ = (grid.wraps ? 2 : down - up) * grid.spacing;
    const double sx =
        runX > 0.0 ? exaggeration * (grid.at(right, j) - grid.at(left, j)) / runX : 0.0;
    const double sz =
        runZ > 0.0 ? exaggeration * (grid.at(i, down) - grid.at(i, up)) / runZ : 0.0;
    const double lambert =
        (0.5 * sx + std::sqrt(0.5) + 0.5 * sz) / std::sqrt(sx * sx + 1.0 + sz * sz);
    return static_cast<float>(std::clamp(0.3 + 0.7 * lambert / std::sqrt(0.5), 0.2, 1.35));
}

void requireWindow(double spacing, int columns, int rows) {
    if (!(std::isfinite(spacing) && spacing > 0.0) || columns <= 0 || rows <= 0) {
        throw std::invalid_argument("relief map: invalid window");
    }
}

// Fills grid.heights row by row, in blocks of rows on the threads.
void sampleRows(const Heightfield& field, ReliefGrid& grid, int threads) {
    grid.heights.assign(
        static_cast<std::size_t>(grid.columns) * static_cast<std::size_t>(grid.rows), 0.0f);
    planet::parallelRows(grid.rows, planet::resolveThreads(threads), [&](int begin, int end) {
        std::vector<double> xs(static_cast<std::size_t>(grid.columns));
        std::vector<double> zs(xs.size());
        for (int i = 0; i < grid.columns; ++i) {
            xs[static_cast<std::size_t>(i)] = grid.x(i);
        }
        for (int j = begin; j < end; ++j) {
            std::fill(zs.begin(), zs.end(), grid.z(j));
            const std::span<float> row(grid.heights.data() + grid.index(0, j), xs.size());
            field.heightsAt(xs, zs, row);
        }
    });
}

} // namespace

ReliefGrid sampleReliefWindow(const Heightfield& field, double centerX, double centerZ,
                              double spacing, int columns, int rows, int threads) {
    requireWindow(spacing, columns, rows);
    ReliefGrid grid;
    grid.columns = columns;
    grid.rows = rows;
    grid.spacing = spacing;
    grid.originX = centerX - 0.5 * columns * spacing;
    grid.originZ = centerZ - 0.5 * rows * spacing;
    sampleRows(field, grid, threads);
    return grid;
}

ReliefGrid sampleReliefWorld(const Heightfield& field, double cellSize, int threads) {
    const WorldSize& world = field.world();
    if (!(std::isfinite(cellSize) && cellSize > 0.0) ||
        std::fmod(world.x, cellSize) != 0.0 || std::fmod(world.z, cellSize) != 0.0) {
        throw std::invalid_argument("relief map: the cells must divide both sides of the world");
    }
    ReliefGrid grid;
    grid.columns = static_cast<int>(world.x / cellSize);
    grid.rows = static_cast<int>(world.z / cellSize);
    grid.spacing = cellSize;
    grid.originX = -0.5 * world.x;
    grid.originZ = -0.5 * world.z;
    grid.wraps = true;
    sampleRows(field, grid, threads);
    return grid;
}

ReliefGrid downsampleRelief(const ReliefGrid& grid, int factor) {
    if (factor < 1) {
        throw std::invalid_argument("relief map: invalid downsampling factor");
    }
    ReliefGrid out;
    out.columns = std::max(1, grid.columns / factor);
    out.rows = std::max(1, grid.rows / factor);
    out.spacing = grid.spacing * factor;
    out.originX = grid.originX;
    out.originZ = grid.originZ;
    out.wraps = grid.wraps && grid.columns % factor == 0 && grid.rows % factor == 0;
    out.heights.resize(static_cast<std::size_t>(out.columns) *
                       static_cast<std::size_t>(out.rows));
    for (int j = 0; j < out.rows; ++j) {
        for (int i = 0; i < out.columns; ++i) {
            double sum = 0.0;
            int count = 0;
            for (int b = j * factor; b < std::min(grid.rows, (j + 1) * factor); ++b) {
                for (int a = i * factor; a < std::min(grid.columns, (i + 1) * factor); ++a) {
                    sum += grid.at(a, b);
                    ++count;
                }
            }
            out.heights[out.index(i, j)] = static_cast<float>(sum / count);
        }
    }
    return out;
}

std::array<float, 3> reliefColor(float height) {
    const Rgb color = height < kSeaLevel ? tint(kSea, height) : tint(kLand, height);
    return {color.r, color.g, color.b};
}

double reliefExaggeration(double spacing) {
    return std::clamp(std::sqrt(spacing / 10.0), 1.0, 30.0);
}

Image renderRelief(const ReliefGrid& grid) {
    Image image{grid.columns, grid.rows, 3, {}};
    image.pixels.reserve(grid.heights.size() * 3);
    const double exaggeration = reliefExaggeration(grid.spacing);
    for (int j = 0; j < grid.rows; ++j) {
        for (int i = 0; i < grid.columns; ++i) {
            const float height = grid.at(i, j);
            const bool sea = height < kSeaLevel;
            const std::array<float, 3> color = reliefColor(height);
            // The sea floor is shaded lightly: shelves, ridges and trenches still show.
            const float light = shading(grid, i, j, exaggeration);
            const float factor = sea ? 0.75f + 0.25f * light : light;
            image.pixels.insert(image.pixels.end(), {toByte(color[0] * factor),
                                                     toByte(color[1] * factor),
                                                     toByte(color[2] * factor)});
        }
    }
    return image;
}

} // namespace terrain

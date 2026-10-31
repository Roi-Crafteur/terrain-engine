#include "terrain/map/map_render.hpp"

#include "../generation/planet/planet_parallel.hpp"

#include <stdexcept>

namespace terrain {

namespace {

std::size_t gridSamples(int width, int height, int stride) {
    const auto columns = static_cast<std::size_t>((width + stride - 1) / stride + 2);
    const auto rows = static_cast<std::size_t>((height + stride - 1) / stride + 2);
    return columns * rows;
}

MapPoint pointAt(const MapFrame& grid, const std::vector<MapSample>& samples, int i, int j) {
    MapPoint p;
    p.x = grid.pixelX(i);
    p.z = grid.pixelZ(j);
    p.stepX = grid.stepX();
    p.stepZ = grid.stepZ();
    p.sample = &samples[mapGridIndex(grid, i, j)];
    p.neighbors[kMapEast] = samples[mapGridIndex(grid, i + 1, j)].ground;
    p.neighbors[kMapWest] = samples[mapGridIndex(grid, i - 1, j)].ground;
    p.neighbors[kMapSouth] = samples[mapGridIndex(grid, i, j + 1)].ground;
    p.neighbors[kMapNorth] = samples[mapGridIndex(grid, i, j - 1)].ground;
    return p;
}

// The color of every pixel of the grid (without its border), row by row.
std::vector<MapColor> composite(const MapFrame& grid, const std::vector<MapSample>& samples,
                                const std::vector<const MapLayerFn*>& layers, int threads) {
    std::vector<MapColor> colors(static_cast<std::size_t>(grid.width()) *
                                 static_cast<std::size_t>(grid.height()));
    planet::parallelRows(grid.height(), threads, [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < grid.width(); ++i) {
                const MapPoint point = pointAt(grid, samples, i, j);
                MapColor color;
                for (const MapLayerFn* layer : layers) {
                    color = over(color, (*layer)(point));
                }
                colors[static_cast<std::size_t>(j) * static_cast<std::size_t>(grid.width()) +
                       static_cast<std::size_t>(i)] = color;
            }
        }
    });
    return colors;
}

// The sample of pixel i of an image of size pixels drawn from count samples: the one whose
// block holds its center, ((2i + 1) count) / (2 size) in integers.
int sampleOf(int i, int count, int size) {
    return static_cast<int>((static_cast<std::int64_t>(2 * i + 1) * count) /
                            (static_cast<std::int64_t>(2) * size));
}

void put(MapImage& image, int i, int j, MapColor c) {
    const std::size_t at =
        4u * (static_cast<std::size_t>(j) * static_cast<std::size_t>(image.width) +
              static_cast<std::size_t>(i));
    image.rgba[at] = c.r;
    image.rgba[at + 1] = c.g;
    image.rgba[at + 2] = c.b;
    image.rgba[at + 3] = c.a;
}

} // namespace

MapColor MapImage::at(int i, int j) const {
    const std::size_t index =
        4u * (static_cast<std::size_t>(j) * static_cast<std::size_t>(width) +
              static_cast<std::size_t>(i));
    return {rgba[index], rgba[index + 1], rgba[index + 2], rgba[index + 3]};
}

int mapStride(int width, int height, std::size_t maxSamples) {
    if (maxSamples < 9) {
        throw std::invalid_argument("map: at least 9 samples per image");
    }
    int stride = 1;
    while (gridSamples(width, height, stride) > maxSamples) {
        ++stride;
    }
    return stride;
}

MapFrame strideFrame(const MapFrame& frame, int stride) {
    if (stride <= 1) {
        return frame;
    }
    return MapFrame(frame.box(), (frame.width() + stride - 1) / stride,
                    (frame.height() + stride - 1) / stride, frame.world());
}

MapImage renderMap(const MapSampler& sampler, const MapFrame& frame, const MapLayers& layers,
                   const std::vector<std::string>& enabled, const MapRenderParams& params) {
    const std::vector<const MapLayerFn*> chosen = layers.enabled(enabled);
    const int threads = planet::resolveThreads(params.threads);
    MapImage image;
    image.width = frame.width();
    image.height = frame.height();
    image.stride = mapStride(frame.width(), frame.height(), params.maxSamples);
    const MapFrame grid = strideFrame(frame, image.stride);
    std::vector<MapSample> samples(mapGridSize(grid));
    sampler.sample(grid, samples, threads, image.stats);
    const std::vector<MapColor> colors = composite(grid, samples, chosen, threads);
    image.rgba.assign(4u * static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height),
                      0);
    for (int j = 0; j < image.height; ++j) {
        const int row = sampleOf(j, grid.height(), image.height);
        for (int i = 0; i < image.width; ++i) {
            const int column = sampleOf(i, grid.width(), image.width);
            put(image, i, j,
                colors[static_cast<std::size_t>(row) * static_cast<std::size_t>(grid.width()) +
                       static_cast<std::size_t>(column)]);
        }
    }
    return image;
}

} // namespace terrain

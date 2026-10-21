#include "terrain/io/horizon_png.hpp"

#include "terrain/io/ecology_png.hpp"
#include "terrain/io/heightmap_png.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <system_error>

namespace terrain {

namespace {

constexpr int kMapPixels = 1024;

// Lambert shading of a ground of this gradient, lit from the north-west and above, in
// [0.25, 1].
float shade(float gradX, float gradZ) {
    const double nx = -gradX;
    const double nz = -gradZ;
    const double length = std::sqrt(nx * nx + 1.0 + nz * nz);
    // Toward the light: (-1, 1, -1) / sqrt(3).
    const double light = (-nx + 1.0 - nz) / (std::sqrt(3.0) * length);
    return static_cast<float>(std::clamp(light, 0.25, 1.0));
}

std::uint8_t scaled(std::uint8_t channel, float factor) {
    return static_cast<std::uint8_t>(std::clamp(channel * factor + 0.5f, 0.0f, 255.0f));
}

// The sample of a level nearest to (x, z), any image.
const HorizonCell* nearestSample(const HorizonLevel& level, double x, double z) {
    const double pitch = level.pitch();
    const auto i = static_cast<std::int64_t>(std::floor(x / pitch + 0.5));
    const auto k = static_cast<std::int64_t>(std::floor(z / pitch + 0.5));
    return level.find({i, k});
}

// Center of pixel i of a square of size pixels around center.
double pixelCenter(double center, int i, int size, double metersPerPixel) {
    return center + (i + 0.5 - size / 2.0) * metersPerPixel;
}

} // namespace

std::optional<HorizonPoint> horizonPointAt(const HorizonClipmap& clipmap, double x, double z,
                                           int firstLevel) {
    for (int n = std::max(firstLevel, 0); n < clipmap.levelCount(); ++n) {
        const HorizonLevel& level = clipmap.level(n);
        const std::optional<float> height = level.heightAt(x, z);
        if (height) {
            return HorizonPoint{*height, nearestSample(level, x, z), n};
        }
    }
    return std::nullopt;
}

HeightGrid levelHeightGrid(const HorizonLevel& level) {
    HeightGrid grid{level.side(), static_cast<double>(level.pitch()), {}};
    grid.heights.reserve(static_cast<std::size_t>(level.side()) *
                         static_cast<std::size_t>(level.side()));
    for (int k = 0; k < level.side(); ++k) {
        for (int i = 0; i < level.side(); ++i) {
            grid.heights.push_back(level.placed() ? level.atWindow(i, k).height : kSeaLevel);
        }
    }
    return grid;
}

HeightGrid horizonMapHeights(const HorizonClipmap& clipmap, double x, double z, int size,
                             double metersPerPixel) {
    HeightGrid grid{size, metersPerPixel, {}};
    grid.heights.reserve(static_cast<std::size_t>(size) * static_cast<std::size_t>(size));
    for (int row = 0; row < size; ++row) {
        const double pz = pixelCenter(z, row, size, metersPerPixel);
        for (int column = 0; column < size; ++column) {
            const double px = pixelCenter(x, column, size, metersPerPixel);
            const std::optional<HorizonPoint> point = horizonPointAt(clipmap, px, pz);
            grid.heights.push_back(point ? point->height : kSeaLevel);
        }
    }
    return grid;
}

Image renderHorizonMaterials(const HorizonClipmap& clipmap, double x, double z, int size,
                             double metersPerPixel) {
    Image image{size, size, 3, {}};
    image.pixels.reserve(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 3);
    for (int row = 0; row < size; ++row) {
        const double pz = pixelCenter(z, row, size, metersPerPixel);
        for (int column = 0; column < size; ++column) {
            const double px = pixelCenter(x, column, size, metersPerPixel);
            const std::optional<HorizonPoint> point = horizonPointAt(clipmap, px, pz);
            if (!point || point->nearest == nullptr) {
                image.pixels.insert(image.pixels.end(), {0, 0, 0});
                continue;
            }
            const HorizonCell& cell = *point->nearest;
            const Rgb8 color = kMaterialRgb[static_cast<std::size_t>(cell.material)];
            const bool water = (cell.flags & kHorizonWater) != 0;
            const float light = water ? 1.0f : shade(cell.gradX, cell.gradZ);
            image.pixels.insert(
                image.pixels.end(),
                {scaled(color[0], light), scaled(color[1], light), scaled(color[2], light)});
        }
    }
    return image;
}

bool exportHorizonMaps(const HorizonClipmap& clipmap, double x, double z,
                       const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return false;
    }
    bool written = true;
    for (int n = 0; n < clipmap.levelCount(); ++n) {
        const Image image = renderHeightmapPreview(levelHeightGrid(clipmap.level(n)));
        written = writePng(directory / ("level_" + std::to_string(n) + ".png"), image) && written;
    }
    const HorizonLevel& coarsest = clipmap.level(clipmap.levelCount() - 1);
    const double metersPerPixel =
        (coarsest.side() - 1.0) * coarsest.pitch() / static_cast<double>(kMapPixels);
    const HeightGrid relief = horizonMapHeights(clipmap, x, z, kMapPixels, metersPerPixel);
    written = writePng(directory / "map_relief.png", renderHeightmapPreview(relief)) && written;
    const Image materials = renderHorizonMaterials(clipmap, x, z, kMapPixels, metersPerPixel);
    written = writePng(directory / "map_materials.png", materials) && written;
    return written;
}

} // namespace terrain

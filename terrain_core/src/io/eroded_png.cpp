#include "terrain/io/eroded_png.hpp"

#include "terrain/det_math.hpp"
#include "terrain/io/heightmap_png.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <system_error>

namespace terrain {

namespace {

// River colors: from small streams to rivers draining 2^6 times the minimum catchment.
constexpr float kStreamRgb[3] = {120.0f, 200.0f, 255.0f};
constexpr float kRiverRgb[3] = {20.0f, 70.0f, 220.0f};
constexpr double kRiverColorDoublings = 6.0;

void setPixel(Image& image, int column, int row, const std::uint8_t (&rgb)[3]) {
    const auto offset = static_cast<std::size_t>((row * image.width + column) * 3);
    std::copy(rgb, rgb + 3, image.pixels.begin() + static_cast<std::ptrdiff_t>(offset));
}

void drawRiver(Image& image, int column, int row, double catchment, double minimum) {
    const double doublings = dmath::log2(std::max(catchment / minimum, 1.0));
    const auto t = static_cast<float>(std::min(doublings / kRiverColorDoublings, 1.0));
    std::uint8_t rgb[3];
    for (int c = 0; c < 3; ++c) {
        const float value = kStreamRgb[c] + (kRiverRgb[c] - kStreamRgb[c]) * t;
        rgb[c] = static_cast<std::uint8_t>(std::lround(value));
    }
    setPixel(image, column, row, rgb);
}

// Smallest catchment among the river cells: the river threshold of the network.
double minimumRiverCatchment(const RiverNetwork& rivers) {
    double minimum = 0.0;
    for (std::size_t i = 0; i < rivers.river.size(); ++i) {
        if (rivers.river[i] != 0 && (minimum == 0.0 || rivers.catchment[i] < minimum)) {
            minimum = rivers.catchment[i];
        }
    }
    return minimum;
}

} // namespace

Image renderRiverMap(const HeightGrid& grid, const RiverNetwork& rivers) {
    // Gray relief: luminance of the shaded preview.
    Image image = renderHeightmapPreview(grid);
    for (int row = 0; row < grid.size; ++row) {
        for (int column = 0; column < grid.size; ++column) {
            const auto offset = static_cast<std::size_t>((row * grid.size + column) * 3);
            if (grid.at(column, row) < kSeaLevel) {
                setPixel(image, column, row, kOceanRgb);
                continue;
            }
            const float luma = 0.299f * image.pixels[offset] + 0.587f * image.pixels[offset + 1] +
                               0.114f * image.pixels[offset + 2];
            const auto gray = static_cast<std::uint8_t>(std::lround(0.8f * luma));
            const std::uint8_t rgb[3] = {gray, gray, gray};
            setPixel(image, column, row, rgb);
        }
    }
    const double minimum = minimumRiverCatchment(rivers);
    for (int index = 0; index < static_cast<int>(rivers.river.size()); ++index) {
        if (rivers.isRiver(index)) {
            drawRiver(image, index % grid.size, index / grid.size,
                      rivers.catchment[static_cast<std::size_t>(index)], minimum);
        }
    }
    for (const Cascade& cascade : rivers.cascades) {
        for (int row = cascade.row - 1; row <= cascade.row + 1; ++row) {
            for (int column = cascade.column - 1; column <= cascade.column + 1; ++column) {
                if (column >= 0 && row >= 0 && column < grid.size && row < grid.size) {
                    setPixel(image, column, row, kCascadeRgb);
                }
            }
        }
    }
    return image;
}

bool exportErodedPngs(const ErodedRegion& region, const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return false;
    }
    const HeightGrid& grid = region.grid();
    const bool gray = writePng(directory / "heightmap_eroded.png", renderHeightmapGray(grid));
    const bool preview =
        writePng(directory / "heightmap_eroded_preview.png", renderHeightmapPreview(grid));
    const bool rivers =
        writePng(directory / "rivers.png", renderRiverMap(grid, region.rivers()));
    return gray && preview && rivers;
}

} // namespace terrain

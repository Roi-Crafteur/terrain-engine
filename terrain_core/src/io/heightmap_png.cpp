#include "terrain/io/heightmap_png.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <system_error>

namespace terrain {

namespace {

struct Rgb {
    float r;
    float g;
    float b;
};

Rgb lerp(Rgb a, Rgb b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

struct ColorStop {
    float height;
    Rgb color;
};

// Land colors by altitude; blue never dominates, so land never reads as water.
constexpr std::array<ColorStop, 7> kLandColors = {{
    {0.0f, {95.0f, 150.0f, 75.0f}},     // lowland green
    {250.0f, {135.0f, 170.0f, 95.0f}},  // plains
    {600.0f, {170.0f, 160.0f, 105.0f}}, // highlands
    {1200.0f, {150.0f, 125.0f, 95.0f}}, // brown slopes
    {1800.0f, {125.0f, 115.0f, 108.0f}}, // rock
    {2400.0f, {220.0f, 220.0f, 220.0f}}, // snow line
    {3000.0f, {255.0f, 255.0f, 255.0f}}, // summits
}};

constexpr Rgb kShallowWater = {120.0f, 180.0f, 225.0f};
constexpr Rgb kDeepWater = {15.0f, 35.0f, 95.0f};

Rgb altitudeColor(float height) {
    if (height < kSeaLevel) {
        const float depth = std::clamp((kSeaLevel - height) / -kMinBaseHeight, 0.0f, 1.0f);
        return lerp(kShallowWater, kDeepWater, std::sqrt(depth));
    }
    if (height >= kLandColors.back().height) {
        return kLandColors.back().color;
    }
    std::size_t i = 0;
    while (height >= kLandColors[i + 1].height) {
        ++i;
    }
    const ColorStop low = kLandColors[i];
    const ColorStop high = kLandColors[i + 1];
    return lerp(low.color, high.color, (height - low.height) / (high.height - low.height));
}

// Lambert factor of the ground normal, relative to flat ground (flat = 1).
float shading(const HeightGrid& grid, int column, int row) {
    const int last = grid.size - 1;
    const int left = std::max(column - 1, 0);
    const int right = std::min(column + 1, last);
    const int up = std::max(row - 1, 0);
    const int down = std::min(row + 1, last);
    const double dx = (right - left) * grid.metersPerPixel;
    const double dz = (down - up) * grid.metersPerPixel;
    const double slopeX = dx > 0.0 ? (grid.at(right, row) - grid.at(left, row)) / dx : 0.0;
    const double slopeZ = dz > 0.0 ? (grid.at(column, down) - grid.at(column, up)) / dz : 0.0;
    // Normal (-slopeX, 1, -slopeZ); light from the north-west (-X, -Z), 45 degrees high.
    const double lightX = -0.5;
    const double lightY = std::sqrt(0.5);
    const double lightZ = -0.5;
    const double lambert = (-slopeX * lightX + lightY - slopeZ * lightZ) /
                           std::sqrt(slopeX * slopeX + 1.0 + slopeZ * slopeZ);
    return static_cast<float>(std::clamp(0.35 + 0.65 * lambert / lightY, 0.2, 1.3));
}

std::uint8_t toByte(float value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 255.0f)));
}

} // namespace

HeightGrid sampleHeightGrid(const Heightfield& field, const HeightmapRegion& region) {
    HeightGrid grid{region.size, region.metersPerPixel, {}};
    grid.heights.reserve(static_cast<std::size_t>(region.size) *
                         static_cast<std::size_t>(region.size));
    const double half = region.size / 2.0;
    for (int row = 0; row < region.size; ++row) {
        const double z = region.centerZ + (row + 0.5 - half) * region.metersPerPixel;
        for (int column = 0; column < region.size; ++column) {
            const double x = region.centerX + (column + 0.5 - half) * region.metersPerPixel;
            grid.heights.push_back(field.heightAt(x, z));
        }
    }
    return grid;
}

std::uint8_t grayLevel(float height) {
    const float t = (height - kHeightmapMinHeight) / (kHeightmapMaxHeight - kHeightmapMinHeight);
    return toByte(255.0f * t);
}

Image renderHeightmapGray(const HeightGrid& grid) {
    Image image{grid.size, grid.size, 1, {}};
    image.pixels.reserve(grid.heights.size());
    for (const float height : grid.heights) {
        image.pixels.push_back(grayLevel(height));
    }
    return image;
}

Image renderHeightmapPreview(const HeightGrid& grid) {
    Image image{grid.size, grid.size, 3, {}};
    image.pixels.reserve(grid.heights.size() * 3);
    for (int row = 0; row < grid.size; ++row) {
        for (int column = 0; column < grid.size; ++column) {
            const float height = grid.at(column, row);
            const float light = height < kSeaLevel ? 1.0f : shading(grid, column, row);
            const Rgb color = altitudeColor(height);
            image.pixels.insert(image.pixels.end(), {toByte(color.r * light),
                                                     toByte(color.g * light),
                                                     toByte(color.b * light)});
        }
    }
    return image;
}

bool exportHeightmapPngs(const Heightfield& field, const HeightmapRegion& region,
                         const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return false;
    }
    const HeightGrid grid = sampleHeightGrid(field, region);
    const bool gray = writePng(directory / "heightmap.png", renderHeightmapGray(grid));
    const bool preview =
        writePng(directory / "heightmap_preview.png", renderHeightmapPreview(grid));
    return gray && preview;
}

} // namespace terrain

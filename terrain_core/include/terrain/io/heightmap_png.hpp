#pragma once

#include "terrain/generation/heightfield.hpp"
#include "terrain/io/png.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace terrain {

// Square area of the world rendered into heightmap images.
struct HeightmapRegion {
    double centerX = 0.0;
    double centerZ = 0.0;
    double metersPerPixel = 100.0;
    int size = 1024; // pixels along each side
};

// Heights at pixel centers: pixel (column i, row j) samples the world at
// (x, z) = center + (i + 0.5 - size / 2, j + 0.5 - size / 2) * metersPerPixel,
// so +X points right and +Z points down in the images.
struct HeightGrid {
    int size = 0;
    double metersPerPixel = 1.0;
    std::vector<float> heights; // row by row, columns vary fastest

    [[nodiscard]] float at(int column, int row) const {
        return heights[static_cast<std::size_t>(row * size + column)];
    }
};

[[nodiscard]] HeightGrid sampleHeightGrid(const Heightfield& field, const HeightmapRegion& region);

// Fixed altitude range of the gray heightmap, so that images stay comparable.
inline constexpr float kHeightmapMinHeight = -3000.0f; // black
inline constexpr float kHeightmapMaxHeight = 5000.0f;  // white

// Gray level of an altitude: linear between the bounds above, clamped outside.
[[nodiscard]] std::uint8_t grayLevel(float height);

// Raw heightmap, one channel.
[[nodiscard]] Image renderHeightmapGray(const HeightGrid& grid);

// Preview for visual checks, RGB: hypsometric colors (blue below sea level, then green,
// brown, rock and snow) shaded by the slope lit from the north-west. Water is not shaded.
[[nodiscard]] Image renderHeightmapPreview(const HeightGrid& grid);

// Writes heightmap.png (gray) and heightmap_preview.png (RGB) into directory, creating it
// if needed. Returns false if a file cannot be written.
[[nodiscard]] bool exportHeightmapPngs(const Heightfield& field, const HeightmapRegion& region,
                                       const std::filesystem::path& directory);

} // namespace terrain

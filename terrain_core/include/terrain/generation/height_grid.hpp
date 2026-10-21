#pragma once

#include "terrain/generation/heightfield.hpp"

#include <cstddef>
#include <vector>

namespace terrain {

// Square area of the world sampled into a height grid: a window in unwrapped coordinates,
// centered anywhere, which may cross an edge of the wrapped world (WorldSize): it then reads
// the periodic world on both sides.
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

    [[nodiscard]] float at(int column, int row) const { return heights[index(column, row)]; }
    [[nodiscard]] float& at(int column, int row) { return heights[index(column, row)]; }

    [[nodiscard]] std::size_t index(int column, int row) const {
        return static_cast<std::size_t>(row * size + column);
    }
};

[[nodiscard]] HeightGrid sampleHeightGrid(const Heightfield& field, const HeightmapRegion& region);

// The region, if its window (size * metersPerPixel per side) is at most as wide as the world
// along each axis. Throws std::invalid_argument otherwise: a region with objects of its own
// (erosion, rivers, cliffs, arches, caves) would hold some points of the world twice.
[[nodiscard]] HeightmapRegion validatedRegion(const HeightmapRegion& region,
                                              const WorldSize& world);

} // namespace terrain

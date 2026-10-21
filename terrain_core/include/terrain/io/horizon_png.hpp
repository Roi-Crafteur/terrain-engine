#pragma once

#include "terrain/generation/height_grid.hpp"
#include "terrain/io/png.hpp"
#include "terrain/streaming/horizon_epoch.hpp"

#include <filesystem>
#include <optional>

namespace terrain {

// Control images of the far horizon: what its grids hold, as a renderer reads them. They only
// read the levels of a clipmap, never the relief itself.

// What the clipmap shows at a point: the ground of the finest level whose window holds it
// (bilinear), the sample of that level nearest to it, and the level.
struct HorizonPoint {
    float height = 0.0f;
    const HorizonCell* nearest = nullptr;
    int level = 0;
};

// The point (x, z), any image, from the finest level from firstLevel on whose window holds it,
// or nullopt outside every window.
[[nodiscard]] std::optional<HorizonPoint> horizonPointAt(const HorizonClipmap& clipmap, double x,
                                                         double z, int firstLevel = 0);

// Heights of a level, one pixel per sample from the low corner of its window: +X right, +Z down.
[[nodiscard]] HeightGrid levelHeightGrid(const HorizonLevel& level);

// Heights of the square of size x size pixels of metersPerPixel around (x, z) (pixel centers as
// HeightGrid), each from the finest level holding it; kSeaLevel outside every level.
[[nodiscard]] HeightGrid horizonMapHeights(const HorizonClipmap& clipmap, double x, double z,
                                           int size, double metersPerPixel);

// Materials of the same square, RGB: the color of the material of the nearest sample of the
// finest level holding each pixel (kMaterialRgb), shaded by its gradient lit from the
// north-west; water is not shaded. Black outside every level.
[[nodiscard]] Image renderHorizonMaterials(const HorizonClipmap& clipmap, double x, double z,
                                           int size, double metersPerPixel);

// Writes into directory, creating it if needed: level_N.png for every level
// (renderHeightmapPreview of its window), map_relief.png and map_materials.png (1024 pixels
// across the window of the coarsest level, around the observer at (x, z)). Returns false if a
// file cannot be written.
[[nodiscard]] bool exportHorizonMaps(const HorizonClipmap& clipmap, double x, double z,
                                     const std::filesystem::path& directory);

} // namespace terrain

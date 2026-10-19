#pragma once

#include "terrain/generation/eroded_region.hpp"
#include "terrain/generation/height_grid.hpp"
#include "terrain/generation/rivers.hpp"
#include "terrain/io/png.hpp"

#include <filesystem>

namespace terrain {

// Colors of the river map.
inline constexpr std::uint8_t kOceanRgb[3] = {20, 30, 70};
inline constexpr std::uint8_t kCascadeRgb[3] = {255, 40, 40};

// River map, RGB: shaded relief in gray on land, oceans in dark blue, river cells in blue
// (light for streams, deep for large rivers) and cascades as red 3 x 3 dots.
[[nodiscard]] Image renderRiverMap(const HeightGrid& grid, const RiverNetwork& rivers);

// Writes heightmap_eroded.png (gray), heightmap_eroded_preview.png (shaded hypsometric
// colors) and rivers.png into directory, creating it if needed. Returns false if a file
// cannot be written.
[[nodiscard]] bool exportErodedPngs(const ErodedRegion& region,
                                    const std::filesystem::path& directory);

} // namespace terrain

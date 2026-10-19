#pragma once

#include "terrain/generation/height_grid.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/io/png.hpp"

#include <cstdint>
#include <filesystem>

namespace terrain {

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

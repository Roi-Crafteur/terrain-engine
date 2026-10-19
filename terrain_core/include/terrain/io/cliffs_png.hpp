#pragma once

#include "terrain/generation/cliff_region.hpp"
#include "terrain/io/png.hpp"

#include <cstdint>
#include <filesystem>

namespace terrain {

// Colors of the cliff preview.
inline constexpr std::uint8_t kCliffWallRgb[3] = {70, 45, 30};   // vertical walls
inline constexpr std::uint8_t kGorgeRgb[3] = {150, 60, 160};     // gorge floors
inline constexpr std::uint8_t kCirqueRgb[3] = {255, 200, 0};     // cirques below cascades

// Cliff mask, one channel: 255 * M at each sample of the region.
[[nodiscard]] Image renderCliffMask(const CliffRegion& region);

// Preview, RGB: the surface of the volume in shaded hypsometric colors, with the vertical
// walls (a drop of at least half a stratum to a neighbor sample) in dark brown, the gorge
// floors in purple, oceans in dark blue, river cells in blue, cirques as yellow rings and
// cascades as red 3 x 3 dots.
[[nodiscard]] Image renderCliffPreview(const CliffRegion& region);

// Writes cliffs_mask.png and cliffs_preview.png into directory, creating it if needed.
// Returns false if a file cannot be written.
[[nodiscard]] bool exportCliffPngs(const CliffRegion& region,
                                   const std::filesystem::path& directory);

} // namespace terrain

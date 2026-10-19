#pragma once

#include "terrain/generation/sculpted_region.hpp"
#include "terrain/io/png.hpp"

#include <cstdint>
#include <filesystem>

namespace terrain {

// Colors of the arch preview.
inline constexpr std::uint8_t kOverhangRgb[3] = {255, 140, 0}; // rock above air
inline constexpr std::uint8_t kArchSpanRgb[3] = {200, 0, 0};   // span of an arch
inline constexpr std::uint8_t kArchPillarRgb[3] = {255, 255, 255};

// Overhang control mask, one channel: 255 * mask at each sample of the region.
[[nodiscard]] Image renderOverhangMask(const SculptedRegion& region);

// Smallest half-length, in pixels, of the segment drawn for an arch.
inline constexpr double kMinArchHalfLength = 1.5;

// Preview, RGB: the cliff preview (renderCliffPreview of the cliff region), with the samples
// where rock lies above air in orange and each arch as a red segment between its two
// pillars, drawn as white pixels. Rivers, oceans and cascades are never drawn over: the
// arches keep away from them, and so do the overhangs.
[[nodiscard]] Image renderArchesPreview(const SculptedRegion& region);

// Writes overhangs_mask.png and arches_preview.png into directory, creating it if needed.
// Returns false if a file cannot be written.
[[nodiscard]] bool exportArchPngs(const SculptedRegion& region,
                                  const std::filesystem::path& directory);

} // namespace terrain

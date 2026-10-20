#pragma once

#include "terrain/generation/cave_region.hpp"
#include "terrain/io/png.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace terrain {

// Colors of the vertical section.
inline constexpr std::uint8_t kCaveSkyRgb[3] = {150, 200, 240};
inline constexpr std::uint8_t kCaveSeaRgb[3] = {35, 95, 175};
inline constexpr std::uint8_t kCaveRockRgb[3] = {170, 160, 150}; // at the ground, darker below
inline constexpr std::uint8_t kCaveGroundRgb[3] = {30, 140, 40}; // the line of the ground
inline constexpr std::uint8_t kCaveRiverRgb[3] = {20, 70, 220};  // ground of a river column
// Cave air, by the component whose void is the smallest at the point.
inline constexpr std::uint8_t kSpaghettiAirRgb[3] = {255, 225, 70};
inline constexpr std::uint8_t kNoodleAirRgb[3] = {80, 235, 235};
inline constexpr std::uint8_t kCheeseAirRgb[3] = {230, 90, 230};
inline constexpr std::uint8_t kRavineAirRgb[3] = {235, 50, 70};
inline constexpr std::uint8_t kEntranceAirRgb[3] = {255, 140, 20};

// Colors of the map: the samples over a cave go from the shallow to the deep tint with the
// depth of the shallowest cave of their column. Rivers use kCaveRiverRgb.
inline constexpr std::uint8_t kCaveShallowRgb[3] = {255, 90, 255};
inline constexpr std::uint8_t kCaveDeepRgb[3] = {60, 0, 110};
inline constexpr std::uint8_t kCaveEntranceMarkRgb[3] = {255, 255, 0};
inline constexpr std::uint8_t kCaveSliceLineRgb[3] = {255, 255, 255};

// Vertical section of the world along a horizontal line, through the point (centerX, centerZ):
// along the x axis, or along the z axis if alongX is false. Pixel (column, row) is the point
// u = center + (column + 0.5 - width / 2) * metersPerPixel of the line, at height
// y = top - (row + 0.5) * metersPerPixel.
struct CaveSlice {
    double centerX = 0.0;
    double centerZ = 0.0;
    bool alongX = true;
    double metersPerPixel = 0.5;
    int width = 1024;
    int height = 320;
    double top = 0.0; // world y of the top edge of the first row
};

// Numbers of the map, filled by renderCavePreview and exportCavePngs when given.
struct CavePreviewStats {
    std::size_t entrances = 0;  // entrances whose site lies inside the region
    double cavedFraction = 0.0; // [0, 1] share of the samples with a cave under them
};

// Section through the middle of the tube of an entrance of the region, along the horizontal
// axis the tube follows closest to: the nearest to the center of the region among the entrances
// whose tube is within 0.4 (rise over run) of an axis, so that it shows lengthwise, or the
// nearest of all if none is. Without entrances (sea, no land): through the center of the
// region, along x. The top lies 16 m over the highest ground of the line and the height
// reaches 16 m under the deepest cave under the lowest ground, at most 640 pixels.
[[nodiscard]] CaveSlice defaultCaveSlice(const CaveRegion& region);

// Section, RGB: sky over the ground, the sea where it lies under kSeaLevel, rock gray and
// darker with the depth below the ground, with the line of the ground one pixel thick (blue
// in the columns of rivers). Where the density is carved by the caves, the air has the color of
// the component whose void is the smallest there. Deterministic. The size of the slice must be
// at least 1 x 1 pixel.
[[nodiscard]] Image renderCaveSlice(const CaveRegion& region, const CaveSlice& slice);

// Map, RGB: one pixel per sample of the eroded region, the shaded hypsometric colors of its
// relief with the rivers in blue. A sample with a cave under it, found by scanning its column
// from just under the minimum roof to just under the maximum depth every 4 m, is tinted by
// the depth of the shallowest one: bright for shallow, dark for deep. The entrances are drawn
// in yellow (the samples within the radius of their tube of their axis or of their chamber of
// their bottom, at least 0.75 pixel) and the line of defaultCaveSlice in white.
[[nodiscard]] Image renderCavePreview(const CaveRegion& region,
                                      CavePreviewStats* stats = nullptr);

// Writes cave_slice.png (defaultCaveSlice) and cave_preview.png into directory, creating it if
// needed. Returns false if a file cannot be written. Entrances and caves are those of the map.
[[nodiscard]] bool exportCavePngs(const CaveRegion& region, const std::filesystem::path& directory,
                                  CavePreviewStats* stats = nullptr);

} // namespace terrain

#pragma once

#include "terrain/generation/cave_region.hpp"
#include "terrain/io/png.hpp"

#include <array>
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
// Lakes: carved air under the water level of its aquifer (ice if the lake is frozen).
inline constexpr std::uint8_t kCaveWaterRgb[3] = {40, 110, 255};
inline constexpr std::uint8_t kCaveIceRgb[3] = {205, 240, 255};
// Lava: carved air under the lava level of its aquifer (sections and maps).
inline constexpr std::uint8_t kCaveLavaRgb[3] = {255, 70, 0};
// Speleothems (stalactites and stalagmites), in limestone or, in the cold, ice.
inline constexpr std::uint8_t kCaveSpeleothemRgb[3] = {250, 245, 225};
inline constexpr std::uint8_t kCaveSpeleothemIceRgb[3] = {170, 225, 255};

// Color of the rock of a cave wall of a material (CaveMaterial): the Surface keeps the color of the
// line of the ground (kCaveGroundRgb), the Water that of a lake (kCaveWaterRgb). The others differ
// from each other and from the rock, which is gray and darker with the depth.
[[nodiscard]] std::array<std::uint8_t, 3> caveMaterialRgb(CaveMaterial material);

// Rock pixels of a section within this many pixels (Euclidean) of cave air, water included, have
// the color of the material of the wall (1.5 m at 0.5 m/px); speleothems within this many meters
// of its plane are drawn.
inline constexpr int kCaveWallReach = 3;
inline constexpr double kCaveSpeleothemReach = 2.0;

// Colors of the map: the samples over a cave go from the shallow to the deep tint with the
// depth of the shallowest cave of their column. Rivers use kCaveRiverRgb.
inline constexpr std::uint8_t kCaveShallowRgb[3] = {255, 90, 255};
inline constexpr std::uint8_t kCaveDeepRgb[3] = {60, 0, 110};
// The network of galleries, mixed over the samples it crosses (a warm cream, light over the
// dark tints of the caves), and the water of its underground rivers (a bright aqua, apart from
// the blues of the lakes kCaveWaterRgb and of the rivers of the surface kCaveRiverRgb; kCaveIceRgb
// if frozen). Lava uses kCaveLavaRgb. A sample crossed by a gallery goes kCaveGalleryShare of the
// way to its color; the rivers and the lava are drawn as they are.
inline constexpr std::uint8_t kCaveGalleryRgb[3] = {255, 240, 190};
inline constexpr std::uint8_t kCaveRiverWaterRgb[3] = {0, 255, 210};
inline constexpr float kCaveGalleryShare = 0.6f;
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
    std::size_t entrances = 0;   // entrances whose site lies inside the region
    double cavedFraction = 0.0;  // [0, 1] share of the samples with a cave under them
    double lakeFraction = 0.0;   // [0, 1] share of the samples with a lake in a cave under them
    // Share of the samples crossed by a gallery, by the water of an underground river in one,
    // and with lava under them (cave regions only: 0 without them).
    double galleryFraction = 0.0;
    double riverFraction = 0.0;
    double lavaFraction = 0.0;
    // Speleothems drawn on the default slice: set by exportCavePngs only (renderCavePreview does
    // not render the slice).
    std::size_t speleothems = 0;
};

// Numbers of the section, filled by renderCaveSlice when given.
struct CaveSliceStats {
    std::size_t speleothems = 0; // drawn: within kCaveSpeleothemReach of the plane, along the line
    // [0, 1] share of the pixels with the water or the ice of a lake or of an underground river
    double waterFraction = 0.0;
    double lavaFraction = 0.0;   // [0, 1] share of the pixels with lava
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
// the component whose void is the smallest there, or kCaveWaterRgb (kCaveIceRgb if frozen) under
// the water level of its aquifer or of the river of its gallery (CaveField::waterDensity), or
// kCaveLavaRgb under the lava level of its aquifer (CaveField::lavaDensity). The rock within
// kCaveWallReach pixels of cave air has the color of its material (caveMaterialRgb of
// caveMaterial: depth below the ground, environment above, normal estimated from the pixels of
// air around it, height above the lake or the lava: magma next to a lava lake), and the
// speleothems within kCaveSpeleothemReach of the plane are drawn as triangles, base 2 * radius,
// from base to tip along y, over the air (limestone, or ice if the speleothem is of ice).
// Deterministic. The size of the slice must be at least 1 x 1 pixel.
[[nodiscard]] Image renderCaveSlice(const CaveRegion& region, const CaveSlice& slice,
                                    CaveSliceStats* stats = nullptr);

// Map, RGB: one pixel per sample of the eroded region, the shaded hypsometric colors of its
// relief with the rivers in blue. A sample with a cave under it, found by scanning its column
// from just under the minimum roof to just under the maximum depth every 4 m, is tinted by
// the depth of the shallowest one: bright for shallow, dark for deep. A sample whose scan also
// finds the water of a lake in the carved air (the same samples, CaveSampler::waterDensity) is
// tinted over that with kCaveWaterRgb, or kCaveIceRgb if the lake is frozen. With cave regions,
// the samples crossed by a gallery (its axis within max(its half width, 0.75 pixel) of the
// sample, caves allowed at the middle of its height there) are mixed with kCaveGalleryRgb, and
// those whose gallery carries the water of a river (deeper than the film of its minimum depth,
// water found on its axis at mid-depth) have kCaveRiverWaterRgb (kCaveIceRgb if frozen): a 4 m
// scan misses a river 1 m deep. Those with lava in the carved air within about 10 m under the
// lava level of their aquifer have kCaveLavaRgb, over the lakes. The entrances are drawn in
// yellow (the samples within the radius of their tube of their axis or of their chamber of
// their bottom, at least 0.75 pixel) and the line of defaultCaveSlice in white.
[[nodiscard]] Image renderCavePreview(const CaveRegion& region,
                                      CavePreviewStats* stats = nullptr);

// Writes cave_slice.png (defaultCaveSlice) and cave_preview.png into directory, creating it if
// needed. Returns false if a file cannot be written. Entrances, caves and lakes are those of the
// map, speleothems those of the slice.
[[nodiscard]] bool exportCavePngs(const CaveRegion& region, const std::filesystem::path& directory,
                                  CavePreviewStats* stats = nullptr);

} // namespace terrain

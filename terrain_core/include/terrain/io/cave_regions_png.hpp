#pragma once

#include "terrain/generation/cave_region.hpp"
#include "terrain/generation/cave_regions.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/io/caves_png.hpp"
#include "terrain/io/png.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace terrain {

// Colors of the district types (index: CaveDistrictType), pairwise distinct, one hue each so
// that a blend of two types stays readable. The map shows the weighted mix of them.
inline constexpr std::array<std::array<std::uint8_t, 3>, kCaveDistrictTypeCount>
    kCaveDistrictRgb = {{
    {235, 200, 45},  // Tunnels: yellow
    {150, 85, 215},  // Halls: violet
    {225, 45, 50},   // Crevasses: red
    {50, 120, 235},  // Lakes: blue
    {40, 205, 195},  // Rivers: turquoise
    {30, 45, 130},   // Flooded: navy
    {250, 125, 25},  // Volcanic: orange
    {225, 245, 255}, // Glacier: ice white
    {70, 190, 70},   // Lush: green
    {150, 105, 65},  // Dripstone: brown
}};

// Color of a district type (kCaveDistrictRgb).
[[nodiscard]] std::array<std::uint8_t, 3> caveDistrictRgb(CaveDistrictType type);

// Marks of the map: the site of a district is a white square (kCaveRegionsSiteReach pixels
// around its pixel, Chebyshev) with a black outline one pixel wide.
inline constexpr std::uint8_t kCaveRegionsSiteRgb[3] = {255, 255, 255};
inline constexpr std::uint8_t kCaveRegionsSiteOutlineRgb[3] = {0, 0, 0};
inline constexpr int kCaveRegionsSiteReach = 1;

// The preview tints each sample with the district mix of its point: the color of the relief
// goes this far toward it (0 = relief, 1 = district color) before the caves are drawn over it.
inline constexpr float kCaveRegionsPreviewTintShare = 0.45f;

// Numbers of the map, filled by renderCaveRegionsMap when given.
struct CaveRegionsMapStats {
    // Share of each district type (index: CaveDistrictType), [0, 1]: the mean weight of the
    // type over the pixels where caves can exist, so they sum to 1 (all 0 without such pixel).
    std::array<double, kCaveDistrictTypeCount> typeShares{};
    int typesPresent = 0;            // types with a non-zero share
    double cavedGroundFraction = 0.0; // [0, 1] share of the pixels where caves can exist
    std::size_t sites = 0;           // sites whose pixel lies inside the map
};

// Numbers of the preview, filled by renderCaveRegionsPreview when given.
struct CaveRegionsPreviewStats {
    CavePreviewStats caves;
    // Mean weight of each district type over all the samples of the preview, [0, 1], sum to 1.
    std::array<double, kCaveDistrictTypeCount> typeShares{};
};

// Map of the district types, RGB: one pixel per sample of area, the point of the pixel
// (x, z) = center + (column + 0.5 - size / 2, row + 0.5 - size / 2) * metersPerPixel. Its color is
// the sum of the weights of the districts there (DistrictWeights) times their colors
// (kCaveDistrictRgb): a smooth gradient across the blend of the borders, which are the Voronoi
// edges of the sites. It is multiplied by a light hillshade of the relief (ground.height; the
// light comes from the north-west and the shade never brightens), by 0.7 where the ground lies
// under the sea and by 0.35 where it lies lower than CaveParams::deepOceanDepth under it, where
// there are no caves; a pixel whose nearest district is not that of its right or lower neighbor
// is darkened by half (the Voronoi borders, one pixel thick) and the sites inside the map are
// marked with kCaveRegionsSiteRgb squares. Deterministic. Throws std::invalid_argument if the
// field has no cave regions (CaveParams::districts), or the area is empty or has a
// non-positive or non-finite metersPerPixel.
[[nodiscard]] Image renderCaveRegionsMap(const CaveField& field, const CaveGround& ground,
                                         const HeightmapRegion& area,
                                         CaveRegionsMapStats* stats = nullptr);

// Map of the caves of a region with cave regions (renderCavePreview without the line of the
// section): the shaded hypsometric relief with the rivers, tinted with the district mix
// at each sample (kCaveRegionsPreviewTintShare of the district colors of the map), then
// the samples over a cave tinted by the depth of the shallowest one, the lakes and the
// entrances as in renderCavePreview. Deterministic. Throws std::invalid_argument if the field
// of the region has no cave regions.
[[nodiscard]] Image renderCaveRegionsPreview(const CaveRegion& region,
                                             CaveRegionsPreviewStats* stats = nullptr);

// A point of the world where two district types meet.
struct CaveRegionsMeeting {
    double x = 0.0;
    double z = 0.0;
    bool found = false; // false: no meeting near the point, (x, z) is the point itself
};

// The meeting of two district types nearest to (nearX, nearZ) among the pairs of neighbor sites
// of the 11 x 11 cells around it: the midpoint of the pair, on land (the ground at the two sites
// and at the midpoint stands at least 10 m over the sea), where the two nearest types weigh
// at least 0.3 each, so that the caves of the two types meet there. The pair whose midpoint is
// the nearest wins, the first in cell order on a tie. Deterministic. Throws
// std::invalid_argument if the field has no cave regions.
[[nodiscard]] CaveRegionsMeeting findCaveRegionsMeeting(const CaveField& field,
                                                        const CaveGround& ground, double nearX,
                                                        double nearZ);

// What exportCaveRegionsPngs renders: the map of the streamed world (the heightfield under
// CaveParams with the default cave regions), and the preview, a CaveRegion with the same
// seed and cave regions, around the nearest meeting of two types to the center of the map.
struct CaveRegionsExport {
    std::uint64_t seed = kDefaultWorldSeed;
    HeightmapRegion map{0.0, 0.0, 100.0, 512}; // 51.2 km
    double previewMetersPerPixel = 20.0;
    int previewSize = 224; // 4.5 km, about fifteen seconds in an unoptimized build
};

struct CaveRegionsStats {
    CaveRegionsMapStats map;
    CaveRegionsPreviewStats preview;
    HeightmapRegion previewArea; // center: the meeting, or the center of the map if none
    bool meetingFound = false;
};

// Writes cave_regions.png (renderCaveRegionsMap) and cave_regions_preview.png
// (renderCaveRegionsPreview) into directory, creating it if needed. Returns false if a file
// cannot be written. Throws std::invalid_argument for an empty map or preview.
[[nodiscard]] bool exportCaveRegionsPngs(const CaveRegionsExport& request,
                                         const std::filesystem::path& directory,
                                         CaveRegionsStats* stats = nullptr);

} // namespace terrain

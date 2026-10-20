#pragma once

// Helpers shared by the vertical section (caves_png_layers.cpp, which draws its layers: ground,
// air, lakes, lava, walls and speleothems), the network of galleries, rivers and lava of the map
// (caves_png_network.cpp) and the rest of the cave images (caves_png.cpp): internal.

#include "terrain/io/caves_png.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace terrain::cave_png {

using Rgb = std::span<const std::uint8_t, 3>;

// A section reaches this far (meters) over the highest ground and under the deepest cave.
inline constexpr double kSliceMargin = 16.0;

struct LinePoint {
    double x;
    double z;
};

// Position along the line of a section of a pixel column (fractions of a pixel are allowed).
[[nodiscard]] double sliceU(const CaveSlice& slice, double column);

// Point of the world at the position u along the line of a section.
[[nodiscard]] LinePoint slicePoint(const CaveSlice& slice, double u);

// Sets a pixel, if it lies inside the image.
void setPixel(Image& image, int column, int row, Rgb rgb);

// Color of a channel mixed toward another: a + (b - a) * t, rounded.
[[nodiscard]] std::uint8_t mix(std::uint8_t a, std::uint8_t b, float t);

// What the scan of a sample of the map finds under it.
struct Scan {
    double depth = -1.0;       // of the shallowest carved air, -1 if the scan finds none
    bool water = false;        // some carved air of the scan lies under the water of a lake
    bool frozen = false;       // the lake is frozen
    bool gallery = false;      // the axis of a gallery crosses the sample (scanNetwork)
    bool river = false;        // ... and its river carries water on the axis, mid-depth
    bool riverFrozen = false;  // the river is frozen
    bool lava = false;         // carved air holds lava within about 10 m under its level
};

// Marks the gallery, the river and the lava that the column of a sample of the map holds into
// scan, as caves_png_network.cpp describes: without cave regions, none. The sampler must hold
// the column, and map samples are metersPerPixel apart.
void scanNetwork(CaveSampler& sampler, const CaveColumn& column, double metersPerPixel,
                 Scan& scan);

// Draws the galleries, then the rivers, then the lava of the scans (one per sample of image).
void drawNetwork(Image& image, const std::vector<Scan>& scans);

// Color toward which a sample over a cave is tinted, from its index in the image and the share
// [0, 1] of the depth of the shallowest cave under it between the roof and the deepest caves.
using CaveTint = std::function<std::array<std::uint8_t, 3>(std::size_t sample, float depth)>;

// The map of cave_preview.png (renderCavePreview): the relief of the region with its rivers,
// then beforeCaves (if set) called on that image, then the caves (tinted toward caveTint if
// set, else from kCaveShallowRgb to kCaveDeepRgb), the lakes and the entrances and, if
// withSliceLine, the line of the default section.
[[nodiscard]] Image renderCaveMap(const CaveRegion& region,
                                  const std::function<void(Image&)>& beforeCaves,
                                  const CaveTint& caveTint, bool withSliceLine,
                                  CavePreviewStats* stats);

} // namespace terrain::cave_png

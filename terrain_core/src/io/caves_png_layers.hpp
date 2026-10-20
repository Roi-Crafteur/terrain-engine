#pragma once

// Helpers shared by the vertical section (caves_png_layers.cpp, which draws its layers: ground,
// air, lakes, walls and speleothems) and the rest of the cave images (caves_png.cpp): internal.

#include "terrain/io/caves_png.hpp"

#include <cstdint>
#include <functional>
#include <span>

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

// The map of cave_preview.png (renderCavePreview): the relief of the region with its rivers,
// then beforeCaves (if set) called on that image, then the caves, the lakes and the entrances
// and, if withSliceLine, the line of the default section.
[[nodiscard]] Image renderCaveMap(const CaveRegion& region,
                                  const std::function<void(Image&)>& beforeCaves,
                                  bool withSliceLine, CavePreviewStats* stats);

} // namespace terrain::cave_png

#pragma once

// Drawing the network over a map (terrain_export_network): a canvas over a box of the world, lines,
// arcs, discs and rings, the colors of the orders of Strahler, the widths by the discharge.

#include "terrain/io/png.hpp"

#include <array>
#include <cstdint>

namespace network_tool {

using Rgb = std::array<std::uint8_t, 3>;

struct Canvas {
    terrain::Image* image = nullptr;
    double x0 = 0.0; // the world at the left and top edges, meters
    double z0 = 0.0;
    double spacing = 1.0; // meters a pixel
};

// The color of an order of Strahler, from 1 (light) to 12 and more (dark).
[[nodiscard]] Rgb orderColor(int order);
// The half width of a stream of this discharge, pixels: a logarithmic scale.
[[nodiscard]] double halfWidth(double discharge);

void disc(const Canvas& c, double x, double z, double radius, Rgb color);
void ring(const Canvas& c, double x, double z, double radius, Rgb color);
void square(const Canvas& c, double x, double z, double half, Rgb color);
// A thick line; dashed: every other 6 pixels left out.
void line(const Canvas& c, double ax, double az, double bx, double bz, double radius, Rgb color,
          bool dashed = false);
// A quadratic arc from (ax, az) to (bx, bz) bent toward (kx, kz).
void arc(const Canvas& c, double ax, double az, double kx, double kz, double bx, double bz,
         double radius, Rgb color, bool dashed = false);
// The image toward white by share in [0, 1] (the relief under the network).
void fade(terrain::Image& image, double share);

} // namespace network_tool

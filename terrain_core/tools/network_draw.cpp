#include "network_draw.hpp"

#include <algorithm>
#include <cmath>

namespace network_tool {

namespace {

void put(const Canvas& c, int px, int pz, Rgb color) {
    terrain::Image& img = *c.image;
    if (px < 0 || pz < 0 || px >= img.width || pz >= img.height) {
        return;
    }
    const auto k = static_cast<std::size_t>((pz * img.width + px) * img.channels);
    for (std::size_t ch = 0; ch < 3; ++ch) {
        img.pixels[k + ch] = color[ch];
    }
}

double pixelX(const Canvas& c, double x) {
    return (x - c.x0) / c.spacing;
}

double pixelZ(const Canvas& c, double z) {
    return (z - c.z0) / c.spacing;
}

void dot(const Canvas& c, double px, double pz, double radius, Rgb color) {
    const int r = static_cast<int>(std::ceil(radius));
    for (int dz = -r; dz <= r; ++dz) {
        for (int dx = -r; dx <= r; ++dx) {
            const double cx = std::floor(px) + dx + 0.5 - px;
            const double cz = std::floor(pz) + dz + 0.5 - pz;
            if (cx * cx + cz * cz <= radius * radius + 0.25) {
                put(c, static_cast<int>(std::floor(px)) + dx, static_cast<int>(std::floor(pz)) + dz,
                    color);
            }
        }
    }
}

} // namespace

Rgb orderColor(int order) {
    static const Rgb kColors[12] = {{170, 215, 240}, {110, 190, 225}, {60, 170, 180},
                                    {80, 180, 90},   {160, 200, 50},  {235, 205, 40},
                                    {245, 150, 40},  {230, 90, 40},   {200, 40, 60},
                                    {165, 40, 160},  {110, 40, 170},  {50, 20, 100}};
    return kColors[std::clamp(order, 1, 12) - 1];
}

double halfWidth(double discharge) {
    return 0.45 + 0.55 * std::log10(1.0 + std::max(discharge, 0.0) * 10.0);
}

void disc(const Canvas& c, double x, double z, double radius, Rgb color) {
    dot(c, pixelX(c, x), pixelZ(c, z), radius, color);
}

void ring(const Canvas& c, double x, double z, double radius, Rgb color) {
    const double px = pixelX(c, x);
    const double pz = pixelZ(c, z);
    const int steps = std::max(12, static_cast<int>(radius * 8.0));
    for (int k = 0; k < steps; ++k) {
        const double a = 6.283185307179586 * k / steps;
        dot(c, px + radius * std::cos(a), pz + radius * std::sin(a), 0.6, color);
    }
}

void square(const Canvas& c, double x, double z, double half, Rgb color) {
    const int px = static_cast<int>(std::floor(pixelX(c, x)));
    const int pz = static_cast<int>(std::floor(pixelZ(c, z)));
    const int h = static_cast<int>(std::ceil(half));
    for (int dz = -h; dz <= h; ++dz) {
        for (int dx = -h; dx <= h; ++dx) {
            put(c, px + dx, pz + dz, color);
        }
    }
}

void line(const Canvas& c, double ax, double az, double bx, double bz, double radius, Rgb color,
          bool dashed) {
    const double pax = pixelX(c, ax);
    const double paz = pixelZ(c, az);
    const double pbx = pixelX(c, bx);
    const double pbz = pixelZ(c, bz);
    const double length = std::hypot(pbx - pax, pbz - paz);
    const int steps = std::max(1, static_cast<int>(std::ceil(length * 2.0)));
    for (int k = 0; k <= steps; ++k) {
        const double t = static_cast<double>(k) / steps;
        if (dashed && static_cast<int>(t * length / 6.0) % 2 == 1) {
            continue;
        }
        dot(c, pax + (pbx - pax) * t, paz + (pbz - paz) * t, radius, color);
    }
}

void arc(const Canvas& c, double ax, double az, double kx, double kz, double bx, double bz,
         double radius, Rgb color, bool dashed) {
    constexpr int kPieces = 6;
    double px = ax;
    double pz = az;
    for (int k = 1; k <= kPieces; ++k) {
        const double t = static_cast<double>(k) / kPieces;
        const double u = 1.0 - t;
        const double x = u * u * ax + 2.0 * u * t * kx + t * t * bx;
        const double z = u * u * az + 2.0 * u * t * kz + t * t * bz;
        line(c, px, pz, x, z, radius, color, dashed);
        px = x;
        pz = z;
    }
}

void fade(terrain::Image& image, double share) {
    for (std::uint8_t& v : image.pixels) {
        v = static_cast<std::uint8_t>(std::lround(v + (255.0 - v) * share));
    }
}

} // namespace network_tool

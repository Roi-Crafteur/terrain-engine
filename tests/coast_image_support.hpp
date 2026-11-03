#pragma once

// The faces of the coasts as an image of the GPU horizon shows them (agent 33, after the check in
// Unreal of e6f198d: a comb of light vertical strokes along the wall of the view 3, 800 m out to
// sea, under a sun 8 degrees high): the level 0 of the horizon (its samples every 8 m, two
// triangles a cell, as its grid) seen from an eye, each pixel lit by the detail normal of the
// material there (its slopes every 2 m, bilinear; faded into the normal of the grid on its faces,
// Level0::normalAt, after the check of d8e97ed), its luminance; along a wall, the luminance of
// each column of pixels over the middle of the wall, and the part of it that varies within a few
// tens of meters (the strokes, coast_far_test).

#include "coast_render_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <vector>

namespace coast_image {

using coast_render::Level0;

struct Eye {
    double x = 0.0;
    double z = 0.0;
    double height = 0.0;
    double heading = 0.0; // degrees, clockwise from -z (north)
    double pitch = 0.0;   // degrees, up
    double fov = 50.0;    // degrees, across the width
};

inline constexpr int kWidth = 1200;
inline constexpr int kHeight = 675;

// The luminance of each pixel (-1 where the image shows the sky), and whether a wall (a slope of
// the grid of kWallSlope at least) shows there.
struct Image {
    std::vector<double> luminance;
    std::vector<unsigned char> wall;
};

inline constexpr double kWallSlope = 1.0;
inline constexpr double kAmbient = 0.3;

struct V3 {
    double x, y, z;
};
inline V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 unit(V3 a) {
    const double l = std::sqrt(dot(a, a));
    return {a.x / l, a.y / l, a.z / l};
}

// The frame of the eye: forward, right, up, and the focal length in pixels.
struct Frame {
    V3 eye, f, r, u;
    double k;
};

inline Frame frameOf(const Eye& e) {
    const double hd = e.heading * std::numbers::pi / 180.0;
    const double pt = e.pitch * std::numbers::pi / 180.0;
    Frame fr;
    fr.eye = {e.x, e.height, e.z};
    fr.f = unit({std::sin(hd) * std::cos(pt), std::sin(pt), -std::cos(hd) * std::cos(pt)});
    fr.r = unit({std::cos(hd), 0.0, std::sin(hd)});
    fr.u = {fr.r.y * fr.f.z - fr.r.z * fr.f.y, fr.r.z * fr.f.x - fr.r.x * fr.f.z,
            fr.r.x * fr.f.y - fr.r.y * fr.f.x};
    fr.k = (kWidth / 2.0) / std::tan(e.fov / 2.0 * std::numbers::pi / 180.0);
    return fr;
}

// A sun elevation degrees high, azimuth degrees to the right of straight behind the eye.
inline V3 sunOf(const Eye& e, double azimuth, double elevation) {
    const Frame fr = frameOf(e);
    const V3 fh = unit({fr.f.x, 0.0, fr.f.z});
    const V3 rh = unit({fr.r.x, 0.0, fr.r.z});
    const double a = azimuth * std::numbers::pi / 180.0;
    const double el = elevation * std::numbers::pi / 180.0;
    return unit({(-fh.x * std::cos(a) + rh.x * std::sin(a)) * std::cos(el), std::sin(el),
                 (-fh.z * std::cos(a) + rh.z * std::sin(a)) * std::cos(el)});
}

// One triangle of the image: projected, rasterized with the world position of each pixel
// (perspective correct), lit by the normal of the material there (Level0::normalAt); steep: a
// wall.
inline void drawTriangle(const Frame& fr, const Level0& l, V3 sun, std::array<V3, 3> v,
                         Image& img, std::vector<double>& depth) {
    const V3 a = v[0], b = v[1], c = v[2];
    const V3 n = unit({(b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y),
                       (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z),
                       (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)});
    const bool steep = std::abs(n.y) < 1.0 / std::sqrt(1.0 + kWallSlope * kWallSlope);
    std::array<double, 3> px{}, py{}, iz{};
    for (std::size_t q = 0; q < 3; ++q) {
        const V3 d = sub(v[q], fr.eye);
        const double zc = dot(d, fr.f);
        if (zc < 1.0) {
            return;
        }
        px[q] = kWidth / 2.0 + dot(d, fr.r) / zc * fr.k;
        py[q] = kHeight / 2.0 - dot(d, fr.u) / zc * fr.k;
        iz[q] = 1.0 / zc;
    }
    const double area = (px[1] - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (py[1] - py[0]);
    if (std::abs(area) < 1e-12) {
        return;
    }
    const int x0 = std::max(0, static_cast<int>(std::floor(std::min({px[0], px[1], px[2]}))));
    const int x1 =
        std::min(kWidth - 1, static_cast<int>(std::ceil(std::max({px[0], px[1], px[2]}))));
    const int y0 = std::max(0, static_cast<int>(std::floor(std::min({py[0], py[1], py[2]}))));
    const int y1 =
        std::min(kHeight - 1, static_cast<int>(std::ceil(std::max({py[0], py[1], py[2]}))));
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double sx = x + 0.5, sy = y + 0.5;
            const double b0 = ((px[1] - sx) * (py[2] - sy) - (px[2] - sx) * (py[1] - sy)) / area;
            const double b1 = ((px[2] - sx) * (py[0] - sy) - (px[0] - sx) * (py[2] - sy)) / area;
            const double b2 = 1.0 - b0 - b1;
            if (b0 < 0.0 || b1 < 0.0 || b2 < 0.0) {
                continue;
            }
            const double zc = 1.0 / (b0 * iz[0] + b1 * iz[1] + b2 * iz[2]);
            const auto at = static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x);
            if (zc >= depth[at]) {
                continue;
            }
            depth[at] = zc;
            const double wx = (b0 * a.x * iz[0] + b1 * b.x * iz[1] + b2 * c.x * iz[2]) * zc;
            const double wz = (b0 * a.z * iz[0] + b1 * b.z * iz[1] + b2 * c.z * iz[2]) * zc;
            const auto [nx, ny, nz] = l.normalAt(wx, wz);
            const V3 dn{nx, ny, nz};
            img.luminance[at] = kAmbient + (1.0 - kAmbient) * std::max(0.0, dot(dn, sun));
            img.wall[at] = steep ? 1 : 0;
        }
    }
}

// The image of the samples of l (the sea at the sea level), lit by sun.
inline Image render(const Level0& l, const Eye& e, V3 sun) {
    const Frame fr = frameOf(e);
    Image img;
    img.luminance.assign(static_cast<std::size_t>(kWidth) * kHeight, -1.0);
    img.wall.assign(img.luminance.size(), 0);
    std::vector<double> depth(img.luminance.size(), std::numeric_limits<double>::infinity());
    const std::int64_t w = l.rect.width();
    const auto pos = [&](std::int64_t i, std::int64_t k) {
        const terrain::HorizonCell& c = l.cells[static_cast<std::size_t>(k * w + i)];
        return V3{static_cast<double>(l.rect.x0 + i) * coast_render::kPitch,
                  std::max(0.0, static_cast<double>(c.height)),
                  static_cast<double>(l.rect.z0 + k) * coast_render::kPitch};
    };
    for (std::int64_t k = 0; k + 1 < l.rect.height(); ++k) {
        for (std::int64_t i = 0; i + 1 < w; ++i) {
            drawTriangle(fr, l, sun, {pos(i, k), pos(i, k + 1), pos(i + 1, k + 1)}, img, depth);
            drawTriangle(fr, l, sun, {pos(i, k), pos(i + 1, k + 1), pos(i + 1, k)}, img, depth);
        }
    }
    return img;
}

// The comb of the wall of an image: the luminance of each column over the middle of its wall
// (from kMiddle to 1 - kMiddle of the rows it covers, at least kLeastRows of them), the part of
// it varying within kWindow columns (its mean around), as a share of the mean luminance (root of
// the mean square); and its light strokes: peaks of that part above kStroke of the mean, at most
// kNarrow columns wide at half their height.
inline constexpr double kMiddle = 0.2;
inline constexpr int kLeastRows = 12;
inline constexpr int kWindow = 41;
inline constexpr double kStroke = 0.06;
inline constexpr int kNarrow = 10;

struct Comb {
    double share = 0.0;
    int strokes = 0;
    int columns = 0;
};

inline Comb combOf(const Image& img) {
    std::vector<double> column(kWidth, -1.0);
    for (int x = 0; x < kWidth; ++x) {
        int first = -1, last = -1;
        for (int y = 0; y < kHeight; ++y) {
            if (img.wall[static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)]) {
                first = first < 0 ? y : first;
                last = y;
            }
        }
        if (first < 0 || last - first + 1 < kLeastRows) {
            continue;
        }
        double sum = 0.0;
        int n = 0;
        for (int y = first; y <= last; ++y) {
            const auto at = static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x);
            const double t = static_cast<double>(y - first) / static_cast<double>(last - first);
            if (img.wall[at] && t >= kMiddle && t <= 1.0 - kMiddle) {
                sum += img.luminance[at];
                ++n;
            }
        }
        column[static_cast<std::size_t>(x)] = n > 0 ? sum / n : -1.0;
    }
    std::vector<double> part(kWidth, 0.0);
    std::vector<unsigned char> has(kWidth, 0);
    double mean = 0.0, square = 0.0;
    Comb out;
    for (int x = 0; x < kWidth; ++x) {
        if (column[static_cast<std::size_t>(x)] < 0.0) {
            continue;
        }
        double sum = 0.0;
        int n = 0;
        const int last = std::min(kWidth - 1, x + kWindow / 2);
        for (int u = std::max(0, x - kWindow / 2); u <= last; ++u) {
            if (column[static_cast<std::size_t>(u)] >= 0.0) {
                sum += column[static_cast<std::size_t>(u)];
                ++n;
            }
        }
        if (n < kWindow * 4 / 5) {
            continue;
        }
        part[static_cast<std::size_t>(x)] = column[static_cast<std::size_t>(x)] - sum / n;
        has[static_cast<std::size_t>(x)] = 1;
        mean += column[static_cast<std::size_t>(x)];
        square += part[static_cast<std::size_t>(x)] * part[static_cast<std::size_t>(x)];
        ++out.columns;
    }
    if (out.columns == 0) {
        return out;
    }
    mean /= out.columns;
    out.share = std::sqrt(square / out.columns) / mean;
    for (int x = 1; x + 1 < kWidth; ++x) {
        const auto at = static_cast<std::size_t>(x);
        const double v = part[at];
        if (!has[at] || v <= kStroke * mean || v < part[at - 1] || v <= part[at + 1]) {
            continue;
        }
        int left = x, right = x;
        while (left > 0 && has[static_cast<std::size_t>(left - 1)] &&
               part[static_cast<std::size_t>(left - 1)] > 0.5 * v) {
            --left;
        }
        while (right + 1 < kWidth && has[static_cast<std::size_t>(right + 1)] &&
               part[static_cast<std::size_t>(right + 1)] > 0.5 * v) {
            ++right;
        }
        out.strokes += right - left + 1 <= kNarrow ? 1 : 0;
    }
    return out;
}

} // namespace coast_image

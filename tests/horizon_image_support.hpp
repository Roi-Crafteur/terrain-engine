#pragma once

// An image of a level of the GPU horizon as its grid and its material draw it (agent 40): the
// triangles of each cell as the grid cuts it (horizon_split.hpp: Fixed, the grids of today, or
// Contour), the ground they show (the sea at the sea level), each pixel lit by the normal of the
// material there: the normals of the grid of its three vertices, linear on its triangle as the GPU
// interpolates them (not bilinear over the cell), leaned toward the detail normal of the level
// (bilinear, faded on the faces of the grid: horizon_detail_normals.py). The eye, the sun, the
// light and the comb of a wall are those of coast_image_support.hpp (agent 33), whose image reads
// the normal of the grid bilinearly over each cell and cuts the cells along their main diagonal,
// where the grid of the GPU cuts them along the other one.

#include "coast_image_support.hpp"
#include "water_horizon_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace horizon_image {

using coast_image::Eye;
using coast_image::Frame;
using coast_image::Image;
using coast_image::V3;
using water_horizon::CellSplit;
using water_horizon::DrawnLevel;

// A vertex of the grid: where it stands and the slopes of its texel.
struct Vertex {
    V3 p;
    double gx, gz;
};

// The detail slopes of the level at (x, z), bilinear (Level0::slopeAt with its detail texels).
inline std::array<double, 2> detailAt(const DrawnLevel& l, double x, double z) {
    constexpr int r = water_horizon::kGpuNormalRatio;
    const double cell = l.pitch / r;
    const std::int64_t width = l.rect.width() * r;
    const std::int64_t height = l.rect.height() * r;
    const double u = x / cell - static_cast<double>(l.rect.x0 * r);
    const double v = z / cell - static_cast<double>(l.rect.z0 * r);
    const auto a =
        std::clamp<std::int64_t>(static_cast<std::int64_t>(std::floor(u)), 0, width - 2);
    const auto b =
        std::clamp<std::int64_t>(static_cast<std::int64_t>(std::floor(v)), 0, height - 2);
    std::array<double, 2> out{};
    for (int q = 0; q < 4; ++q) {
        const std::int64_t i = a + (q & 1);
        const std::int64_t k = b + (q >> 1);
        const double fu = std::clamp(u - static_cast<double>(a), 0.0, 1.0);
        const double fv = std::clamp(v - static_cast<double>(b), 0.0, 1.0);
        const double w = ((q & 1) ? fu : 1.0 - fu) * ((q >> 1) ? fv : 1.0 - fv);
        const auto& d = l.detail[static_cast<std::size_t>(k * width + i)];
        out[0] += w * d.gradX;
        out[1] += w * d.gradZ;
    }
    return out;
}

// The normal of the material from the slopes of the grid g at (x, z) (horizon_detail_normals.py:
// HORIZON_HLSL): the detail normal there, faded into the normal of the grid on its faces.
inline V3 materialNormal(const DrawnLevel& l, double gx, double gz, double x, double z) {
    const V3 n = coast_image::unit({-gx, 1.0, -gz});
    if (l.detail.empty()) {
        return n;
    }
    const auto [dx, dz] = detailAt(l, x, z);
    const V3 d = coast_image::unit({-dx, 1.0, -dz});
    const double w = std::clamp((n.y - coast_render::kSteepNone) /
                                    (coast_render::kSteepFull - coast_render::kSteepNone),
                                0.0, 1.0);
    return coast_image::unit({n.x + w * (d.x - n.x), n.y + w * (d.y - n.y), n.z + w * (d.z - n.z)});
}

// One triangle: projected, rasterized with the world position and the slopes of the grid of each
// pixel (perspective correct, linear on the triangle), lit by the normal of the material there.
inline void drawTriangle(const Frame& fr, const DrawnLevel& l, V3 sun,
                         const std::array<Vertex, 3>& v, Image& img, std::vector<double>& depth) {
    const V3 a = v[0].p, b = v[1].p, c = v[2].p;
    const V3 n = coast_image::unit({(b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y),
                                    (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z),
                                    (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)});
    const bool steep = std::abs(n.y) < 1.0 / std::sqrt(1.0 + coast_image::kWallSlope *
                                                             coast_image::kWallSlope);
    std::array<double, 3> px{}, py{}, iz{};
    for (std::size_t q = 0; q < 3; ++q) {
        const V3 d = coast_image::sub(v[q].p, fr.eye);
        const double zc = coast_image::dot(d, fr.f);
        if (zc < 1.0) {
            return;
        }
        px[q] = coast_image::kWidth / 2.0 + coast_image::dot(d, fr.r) / zc * fr.k;
        py[q] = coast_image::kHeight / 2.0 - coast_image::dot(d, fr.u) / zc * fr.k;
        iz[q] = 1.0 / zc;
    }
    const double area = (px[1] - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (py[1] - py[0]);
    if (std::abs(area) < 1e-12) {
        return;
    }
    const int x0 = std::max(0, static_cast<int>(std::floor(std::min({px[0], px[1], px[2]}))));
    const int x1 = std::min(coast_image::kWidth - 1,
                            static_cast<int>(std::ceil(std::max({px[0], px[1], px[2]}))));
    const int y0 = std::max(0, static_cast<int>(std::floor(std::min({py[0], py[1], py[2]}))));
    const int y1 = std::min(coast_image::kHeight - 1,
                            static_cast<int>(std::ceil(std::max({py[0], py[1], py[2]}))));
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
            const auto at = static_cast<std::size_t>(y) * coast_image::kWidth +
                            static_cast<std::size_t>(x);
            if (zc >= depth[at]) {
                continue;
            }
            depth[at] = zc;
            const auto mix = [&](double e0, double e1, double e2) {
                return (b0 * e0 * iz[0] + b1 * e1 * iz[1] + b2 * e2 * iz[2]) * zc;
            };
            const V3 dn = materialNormal(l, mix(v[0].gx, v[1].gx, v[2].gx),
                                         mix(v[0].gz, v[1].gz, v[2].gz), mix(a.x, b.x, c.x),
                                         mix(a.z, b.z, c.z));
            const double lit = std::max(0.0, coast_image::dot(dn, sun));
            img.luminance[at] = coast_image::kAmbient + (1.0 - coast_image::kAmbient) * lit;
            img.wall[at] = steep ? 1 : 0;
        }
    }
}

// The image of a drawn level (the sea at the sea level, as the grid of today shows it), its cells
// cut as split says, lit by sun.
inline Image render(const DrawnLevel& l, const Eye& e, V3 sun, CellSplit split) {
    const Frame fr = coast_image::frameOf(e);
    Image img;
    img.luminance.assign(static_cast<std::size_t>(coast_image::kWidth) * coast_image::kHeight,
                         -1.0);
    img.wall.assign(img.luminance.size(), 0);
    std::vector<double> depth(img.luminance.size(), std::numeric_limits<double>::infinity());
    const auto vertex = [&](std::int64_t i, std::int64_t k) {
        const auto& t = l.at(i, k);
        return Vertex{{static_cast<double>(l.rect.x0 + i) * l.pitch,
                       std::max(0.0, static_cast<double>(t.height)),
                       static_cast<double>(l.rect.z0 + k) * l.pitch},
                      t.gradX, t.gradZ};
    };
    for (std::int64_t k = 0; k + 1 < l.rect.height(); ++k) {
        for (std::int64_t i = 0; i + 1 < l.rect.width(); ++i) {
            const Vertex v00 = vertex(i, k), v10 = vertex(i + 1, k);
            const Vertex v01 = vertex(i, k + 1), v11 = vertex(i + 1, k + 1);
            if (l.mainOf(i, k, split, water_horizon::SeaDrawing::InGround)) {
                drawTriangle(fr, l, sun, {v00, v10, v11}, img, depth);
                drawTriangle(fr, l, sun, {v00, v11, v01}, img, depth);
            } else {
                drawTriangle(fr, l, sun, {v00, v01, v10}, img, depth);
                drawTriangle(fr, l, sun, {v10, v01, v11}, img, depth);
            }
        }
    }
    return img;
}

} // namespace horizon_image

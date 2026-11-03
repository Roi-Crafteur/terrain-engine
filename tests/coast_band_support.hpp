#pragma once

// The bands of the rock as the materials draw them (agent 33): Look.Bands of the temporary terrain
// material (terrain_unreal/Scripts/temporary_terrain_hlsl.py), the same formula, on the bands the
// core gives the vertices of the tiles (TileData::rockBands) and the samples of the horizon
// (horizon_bands.hpp); and an image of tiles as Unreal draws them: their vertex normals and their
// bands interpolated over their triangles, lit by a sun (coast_bands_test).

#include "coast_image_support.hpp"

#include "terrain/streaming/stream_manager.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <span>
#include <vector>

namespace coast_bands {

using coast_image::Eye;
using coast_image::Frame;
using coast_image::V3;
using coast_image::kHeight;
using coast_image::kWidth;

inline double smoothstep(double e0, double e1, double x) {
    const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

inline double fract(double x) {
    return x - std::floor(x);
}

// A draw in [0, 1) of bed k (the hash of Look.Bands).
inline double bedDraw(double k, double a, double b) {
    return fract(std::sin(k * a + b) * 43758.5453);
}

// Look.Bands: the share of its light the rock keeps under its bands, b at the pixel, w the
// derivative of its strata over a pixel (fwidth; 0: an image fine enough to draw every lamina).
inline double bandsOf(const terrain::RockBands& b, double w) {
    w = std::max(w, 1e-4);
    const double strata = static_cast<double>(b.strata);
    const double beds = static_cast<double>(b.layered) * std::clamp((0.35 - w) / 0.2, 0.0, 1.0);
    const double k = std::floor(strata);
    const double f = strata - k;
    const double tone = 1.0 + 0.24 * (fract(std::sin(k * 12.9898 + 4.1) * 43758.5453) - 0.5);
    const double depth = 0.18 + 0.24 * bedDraw(k, 93.989, 0.3);
    const double bedLine =
        1.0 - depth * (1.0 - smoothstep(0.0, std::max(1.5 * w, 0.03) + 0.06, f));
    const double count = 2.0 + std::floor(5.0 * bedDraw(k, 78.233, 1.7));
    const double warp = 0.8 * (bedDraw(k, 39.425, 2.9) - 0.5);
    const double l = count * (f + 2.0 * warp * f * (1.0 - f));
    const double lw = std::max(count * w, 1e-4);
    const double lamina = 1.0 - 0.12 *
                                    (1.0 - smoothstep(0.0, std::max(1.5 * lw, 0.05) + 0.1,
                                                      fract(l))) *
                                    std::clamp((0.35 - lw) / 0.2, 0.0, 1.0);
    const double shade = 1.0 + (tone * bedLine * lamina - 1.0) * beds;
    const double nh = static_cast<double>(b.notchHeight);
    const double n = static_cast<double>(b.notch);
    const double notch =
        nh > 0.0 ? smoothstep(-0.4, 0.2, n) * (1.0 - smoothstep(nh - 0.2, nh + 0.4, n)) : 0.0;
    const double cap = b.capHeight > 0.0f
                           ? 1.0 - smoothstep(0.0, 0.6, std::abs(static_cast<double>(b.cap)))
                           : 0.0;
    return shade * (1.0 - 0.45 * notch) * (1.0 - 0.3 * cap);
}

// The bands b interpolated: the weights w of three of them.
inline terrain::RockBands mix(const std::array<const terrain::RockBands*, 3>& b,
                              const std::array<double, 3>& w) {
    const auto at = [&](auto field) {
        double v = 0.0;
        for (std::size_t q = 0; q < 3; ++q) {
            v += w[q] * static_cast<double>((*b[q]).*field);
        }
        return static_cast<float>(v);
    };
    return {at(&terrain::RockBands::strata), at(&terrain::RockBands::layered),
            at(&terrain::RockBands::notch),  at(&terrain::RockBands::notchHeight),
            at(&terrain::RockBands::cap),    at(&terrain::RockBands::capHeight)};
}

// Rasterizes triangle v of the eye fr: for each pixel in front of depth, its index and the
// perspective correct weights of the three vertices (and depth set).
inline void rasterize(const Frame& fr, const std::array<V3, 3>& v, std::vector<double>& depth,
                      const std::function<void(std::size_t, const std::array<double, 3>&)>& fn) {
    std::array<double, 3> px{}, py{}, iz{};
    for (std::size_t q = 0; q < 3; ++q) {
        const V3 d = coast_image::sub(v[q], fr.eye);
        const double zc = coast_image::dot(d, fr.f);
        if (zc < 1.0) {
            return;
        }
        px[q] = kWidth / 2.0 + coast_image::dot(d, fr.r) / zc * fr.k;
        py[q] = kHeight / 2.0 - coast_image::dot(d, fr.u) / zc * fr.k;
        iz[q] = 1.0 / zc;
    }
    const double area = (px[1] - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (py[1] - py[0]);
    if (std::abs(area) < 1e-12) {
        return;
    }
    const auto lo = [](double a, double b, double c) { return std::floor(std::min({a, b, c})); };
    const auto hi = [](double a, double b, double c) { return std::ceil(std::max({a, b, c})); };
    const int x0 = std::max(0, static_cast<int>(lo(px[0], px[1], px[2])));
    const int x1 = std::min(kWidth - 1, static_cast<int>(hi(px[0], px[1], px[2])));
    const int y0 = std::max(0, static_cast<int>(lo(py[0], py[1], py[2])));
    const int y1 = std::min(kHeight - 1, static_cast<int>(hi(py[0], py[1], py[2])));
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
            fn(at, {b0 * iz[0] * zc, b1 * iz[1] * zc, b2 * iz[2] * zc});
        }
    }
}

// An image of tiles: per pixel (sky: lit below 0) the light of its normal, its share of rock, its
// bands, its height; its luminance with the bands (luminance).
struct TileImage {
    std::vector<double> lit;
    std::vector<double> rock;
    std::vector<terrain::RockBands> bands;
    std::vector<double> height;
    std::vector<V3> point;  // world
    std::vector<V3> normal; // unit

    // The luminance of each pixel, the rock under its bands (w from the strata of the pixels next
    // to it, as fwidth); -1 for the sky. Without bands: none drawn.
    [[nodiscard]] std::vector<double> luminance(bool withBands) const {
        std::vector<double> out(lit.size(), -1.0);
        for (int y = 0; y + 1 < kHeight; ++y) {
            for (int x = 0; x + 1 < kWidth; ++x) {
                const auto at = static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x);
                if (lit[at] < 0.0) {
                    continue;
                }
                const double s = static_cast<double>(bands[at].strata);
                const double w = std::abs(static_cast<double>(bands[at + 1].strata) - s) +
                                 std::abs(static_cast<double>(bands[at + kWidth].strata) - s);
                const double shade = withBands ? bandsOf(bands[at], w) : 1.0;
                out[at] = lit[at] * (1.0 - rock[at] + rock[at] * shade);
            }
        }
        return out;
    }
};

inline TileImage renderTiles(std::span<const terrain::TileData> tiles, const Eye& e, V3 sun) {
    const Frame fr = coast_image::frameOf(e);
    TileImage img;
    const std::size_t n = static_cast<std::size_t>(kWidth) * kHeight;
    img.lit.assign(n, -1.0);
    img.rock.assign(n, 0.0);
    img.bands.assign(n, {});
    img.height.assign(n, 0.0);
    img.point.assign(n, {0.0, 0.0, 0.0});
    img.normal.assign(n, {0.0, 1.0, 0.0});
    std::vector<double> depth(n, std::numeric_limits<double>::infinity());
    const terrain::RockBands none{};
    for (const terrain::TileData& t : tiles) {
        const terrain::Mesh& m = t.mesh;
        const auto world = [&](std::uint32_t i) {
            return V3{t.originX + m.vertices[i].x, t.originY + m.vertices[i].y,
                      t.originZ + m.vertices[i].z};
        };
        for (std::size_t q = 0; q + 2 < m.indices.size(); q += 3) {
            const std::array<std::uint32_t, 3> i{m.indices[q], m.indices[q + 1], m.indices[q + 2]};
            rasterize(fr, {world(i[0]), world(i[1]), world(i[2])}, depth,
                      [&](std::size_t at, const std::array<double, 3>& w) {
                          V3 nrm{0.0, 0.0, 0.0};
                          V3 pos{0.0, 0.0, 0.0};
                          double rock = 0.0;
                          for (std::size_t c = 0; c < 3; ++c) {
                              const terrain::Vec3f& v = m.normals[i[c]];
                              const V3 wp = world(i[c]);
                              nrm = {nrm.x + w[c] * v.x, nrm.y + w[c] * v.y, nrm.z + w[c] * v.z};
                              pos = {pos.x + w[c] * wp.x, pos.y + w[c] * wp.y, pos.z + w[c] * wp.z};
                              rock += t.rock.empty() ? 0.0 : w[c] * t.rock[i[c]];
                          }
                          const auto band = [&](std::size_t c) {
                              return t.rockBands.empty() ? &none : &t.rockBands[i[c]];
                          };
                          img.lit[at] = coast_image::kAmbient +
                                        (1.0 - coast_image::kAmbient) *
                                            std::max(0.0, coast_image::dot(coast_image::unit(nrm),
                                                                           sun));
                          img.rock[at] = rock;
                          img.bands[at] = mix({band(0), band(1), band(2)}, w);
                          img.height[at] = pos.y;
                          img.point[at] = pos;
                          img.normal[at] = coast_image::unit(nrm);
                      });
        }
    }
    return img;
}

} // namespace coast_bands

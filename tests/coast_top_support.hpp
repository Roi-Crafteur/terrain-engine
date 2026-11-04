#pragma once

// Along the top of the walls of the coasts (agent 33, coast_top_test): signals sampled along the
// line of a face (coast_render_support.hpp: lineOf), detrended, their autocorrelation and their
// part at the lattice of the cells; the triangles of tiles for rays, where a ray first meets them
// with the normal of their vertices interpolated there (the normal Unreal shades with), and
// whether a ray toward the sun meets any (the shadow the engine casts from the triangles).

#include "coast_image_support.hpp"

#include "terrain/streaming/stream_manager.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <utility>
#include <vector>

namespace coast_top {

using coast_image::V3;

// v less its mean over trend meters (samples every kStep along the line; NaN where unseen).
inline std::vector<double> detrended(const std::vector<double>& v, double trend) {
    const auto half = static_cast<int>(trend / 2.0 / coast_render::kStep);
    const auto size = static_cast<int>(v.size());
    std::vector<double> out(v.size(), std::numeric_limits<double>::quiet_NaN());
    for (int i = 0; i < size; ++i) {
        double sum = 0.0;
        int n = 0;
        for (int j = std::max(0, i - half); j <= std::min(size - 1, i + half); ++j) {
            if (!std::isnan(v[static_cast<std::size_t>(j)])) {
                sum += v[static_cast<std::size_t>(j)];
                ++n;
            }
        }
        if (n > half && !std::isnan(v[static_cast<std::size_t>(i)])) {
            out[static_cast<std::size_t>(i)] = v[static_cast<std::size_t>(i)] - sum / n;
        }
    }
    return out;
}

// The autocorrelation of h at lag meters, and the root of its mean square.
inline double autocorrelation(const std::vector<double>& h, double lag, double& rms) {
    const auto l = static_cast<std::size_t>(std::lround(lag / coast_render::kStep));
    double power = 0.0, product = 0.0;
    int n = 0, pairs = 0;
    for (std::size_t i = 0; i < h.size(); ++i) {
        if (std::isnan(h[i])) {
            continue;
        }
        power += h[i] * h[i];
        ++n;
        if (i + l < h.size() && !std::isnan(h[i + l])) {
            product += h[i] * h[i + l];
            ++pairs;
        }
    }
    rms = n > 0 ? std::sqrt(power / n) : 0.0;
    return pairs > n / 2 && power > 0.0 ? (product / pairs) / (power / n) : 0.0;
}

// The part of h at the lattice of the cells of its tiles: h folded on the phase of each sample in
// its cell along x and z (kBins x kBins), the root of the mean square of the means of the phases.
// A pattern of the cells (a step, a stripe at each cell the face crosses) comes back at the same
// phase; anything else averages out, to its mean square times the number of phases over the
// number of samples, taken out (a wall of uneven top shows no part of it at the lattice). Along an
// axis the face crosses the cells of more slowly than every trend meters (the window h is
// detrended over), no phase (it would fold the shape of the face, longer, on a few runs of it).
inline constexpr int kBins = 4;

inline double latticePart(const std::vector<coast_render::Point>& line,
                          const std::vector<double>& h, const std::vector<double>& cells,
                          double trend) {
    std::vector<double> sum(kBins * kBins, 0.0);
    std::vector<int> count(kBins * kBins, 0);
    int n = 0;
    double all = 0.0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (std::isnan(h[i]) || !(cells[i] > 0.0)) {
            continue;
        }
        const auto phase = [&](double v) {
            const double f = v / cells[i] - std::floor(v / cells[i]);
            return std::min(kBins - 1, static_cast<int>(f * kBins));
        };
        const double x = line[i].x + coast_render::kInland * line[i].nx;
        const double z = line[i].z + coast_render::kInland * line[i].nz;
        const bool acrossX = cells[i] <= trend * std::abs(line[i].nz);
        const bool acrossZ = cells[i] <= trend * std::abs(line[i].nx);
        const auto bin = static_cast<std::size_t>((acrossX ? phase(x) : 0) * kBins +
                                                  (acrossZ ? phase(z) : 0));
        sum[bin] += h[i];
        ++count[bin];
        ++n;
        all += h[i] * h[i];
    }
    double power = 0.0;
    int phases = 0;
    for (std::size_t b = 0; b < sum.size(); ++b) {
        power += count[b] > 0 ? sum[b] * sum[b] / count[b] : 0.0;
        phases += count[b] > 0 ? 1 : 0;
    }
    return n > 0 ? std::sqrt(std::max(0.0, power - phases * all / n) / n) : 0.0;
}


// A hit: how far along the ray, and the normal of the vertices there (unit).
struct Hit {
    double t = -1.0;
    V3 normal{0.0, 1.0, 0.0};
};

class Triangles {
public:
    explicit Triangles(std::span<const terrain::TileData> tiles) {
        for (const terrain::TileData& t : tiles) {
            const auto at = [&](std::uint32_t i) {
                return V3{t.originX + t.mesh.vertices[i].x, t.originY + t.mesh.vertices[i].y,
                          t.originZ + t.mesh.vertices[i].z};
            };
            const auto normal = [&](std::uint32_t i) {
                return V3{t.mesh.normals[i].x, t.mesh.normals[i].y, t.mesh.normals[i].z};
            };
            for (std::size_t q = 0; q + 2 < t.mesh.indices.size(); q += 3) {
                const std::array<std::uint32_t, 3> i{t.mesh.indices[q], t.mesh.indices[q + 1],
                                                     t.mesh.indices[q + 2]};
                add({at(i[0]), at(i[1]), at(i[2])}, {normal(i[0]), normal(i[1]), normal(i[2])});
            }
        }
    }

    // The first triangle along o + t d (d unit) within reach meters.
    [[nodiscard]] Hit cast(V3 o, V3 d, double reach) const {
        Hit best;
        ++ray_; // each triangle tried once per ray (a stamp: one ray at a time)
        const double flat = std::max(std::hypot(d.x, d.z), 1e-6);
        for (double s = 0.0; s <= reach * flat + kBucket; s += kBucket / 2.0) {
            const double t = s / flat;
            const auto a = static_cast<std::int64_t>(std::floor((o.x + d.x * t) / kBucket));
            const auto b = static_cast<std::int64_t>(std::floor((o.z + d.z * t) / kBucket));
            for (std::int64_t da = -1; da <= 1; ++da) {
                for (std::int64_t db = -1; db <= 1; ++db) {
                    const auto it = buckets_.find({a + da, b + db});
                    for (std::size_t n = 0; it != buckets_.end() && n < it->second.size(); ++n) {
                        const std::size_t k = it->second[n];
                        if (stamps_[k] != ray_) {
                            stamps_[k] = ray_;
                            hit(k, o, d, reach, best);
                        }
                    }
                }
            }
            if (best.t >= 0.0 && best.t * flat < s - 2.0 * kBucket) {
                break;
            }
        }
        return best;
    }

private:
    static constexpr double kBucket = 4.0;

    struct Triangle {
        std::array<V3, 3> v;
        std::array<V3, 3> n;
    };

    void add(const std::array<V3, 3>& v, const std::array<V3, 3>& n) {
        const std::size_t k = triangles_.size();
        triangles_.push_back({v, n});
        stamps_.push_back(0);
        const auto lo = [](double a, double b, double c) {
            return static_cast<std::int64_t>(std::floor(std::min({a, b, c}) / kBucket));
        };
        const auto hi = [](double a, double b, double c) {
            return static_cast<std::int64_t>(std::floor(std::max({a, b, c}) / kBucket));
        };
        for (std::int64_t a = lo(v[0].x, v[1].x, v[2].x); a <= hi(v[0].x, v[1].x, v[2].x); ++a) {
            for (std::int64_t b = lo(v[0].z, v[1].z, v[2].z); b <= hi(v[0].z, v[1].z, v[2].z);
                 ++b) {
                buckets_[{a, b}].push_back(k);
            }
        }
    }

    // Moller-Trumbore.
    void hit(std::size_t k, V3 o, V3 d, double reach, Hit& best) const {
        const Triangle& tri = triangles_[k];
        const V3 e1 = coast_image::sub(tri.v[1], tri.v[0]);
        const V3 e2 = coast_image::sub(tri.v[2], tri.v[0]);
        const V3 p{d.y * e2.z - d.z * e2.y, d.z * e2.x - d.x * e2.z, d.x * e2.y - d.y * e2.x};
        const double det = coast_image::dot(e1, p);
        if (std::abs(det) < 1e-12) {
            return;
        }
        const V3 s = coast_image::sub(o, tri.v[0]);
        const double u = coast_image::dot(s, p) / det;
        const V3 q{s.y * e1.z - s.z * e1.y, s.z * e1.x - s.x * e1.z, s.x * e1.y - s.y * e1.x};
        const double v = coast_image::dot(d, q) / det;
        const double t = coast_image::dot(e2, q) / det;
        if (u < 0.0 || v < 0.0 || u + v > 1.0 || t <= 1e-4 || t > reach ||
            (best.t >= 0.0 && t >= best.t)) {
            return;
        }
        const double w = 1.0 - u - v;
        best.t = t;
        best.normal = coast_image::unit({w * tri.n[0].x + u * tri.n[1].x + v * tri.n[2].x,
                                         w * tri.n[0].y + u * tri.n[1].y + v * tri.n[2].y,
                                         w * tri.n[0].z + u * tri.n[1].z + v * tri.n[2].z});
    }

    std::vector<Triangle> triangles_;
    mutable std::vector<std::uint64_t> stamps_;
    mutable std::uint64_t ray_ = 0;
    std::map<std::pair<std::int64_t, std::int64_t>, std::vector<std::size_t>> buckets_;
};

} // namespace coast_top

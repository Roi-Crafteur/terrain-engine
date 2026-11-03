#pragma once

// The faces of the coasts as the horizon and the tiles light them (agent 33): the line of a face
// (traced on the distance of its columns to the plane of their face, every kStep meters), the
// samples of the level 0 of the horizon and their detail slopes around it, the parts of values
// repeating at a lattice (coast_render_test, coast_far_test).

#include "coast_planet_support.hpp"
#include "coast_rock_support.hpp"

#include "terrain/generation/ecology.hpp"
#include "terrain/streaming/horizon_levels.hpp"
#include "terrain/streaming/horizon_normals.hpp"
#include "terrain/streaming/horizon_sampler.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace coast_render {

using terrain::HorizonRect;

// The line is traced kInland meters behind the plane of the face, on the columns of its rock (those
// of the sea before the faces of the fjords and of the reefs hold none).
inline constexpr double kInland = 1.0;
inline constexpr double kStep = 0.5;
inline constexpr int kRatio = 4;
inline constexpr double kPitch = 8.0; // level 0
inline constexpr double kDegrees = 180.0 / std::numbers::pi;

inline const terrain::Heightfield& field() {
    return coast_planet::field();
}

// A point of the line of a face: where, the normal of the face there (horizontal, toward the
// air), its heights, its arc length.
struct Point {
    double x, z, nx, nz, foot, top, s;
};

// The distance of (x, z) to the plane of its heaviest face, its foot and top; false without one.
inline bool faceAt(double x, double z, double& distance, double& foot, double& top) {
    const terrain::RockColumn r = field().rockAt(x, z);
    float weight = 0.5f;
    bool found = false;
    for (std::size_t i = 0; i < r.faceCount; ++i) {
        if (r.faces[i].weight > weight) {
            weight = r.faces[i].weight;
            distance = static_cast<double>(r.faces[i].distance);
            foot = static_cast<double>(r.faces[i].foot);
            top = static_cast<double>(r.faces[i].top);
            found = true;
        }
    }
    return found;
}

// Moves (x, z) onto the plane of its face (a Newton step on its distance); its normal there.
inline bool onFace(double& x, double& z, Point& p) {
    double d = 0.0, foot = 0.0, top = 0.0, dx = 0.0, dz = 0.0, f = 0.0, t = 0.0;
    for (int i = 0; i < 3; ++i) {
        if (!faceAt(x, z, d, foot, top) || !faceAt(x + 0.5, z, dx, f, t) ||
            !faceAt(x, z + 0.5, dz, f, t)) {
            return false;
        }
        const double gx = (dx - d) / 0.5;
        const double gz = (dz - d) / 0.5;
        const double g2 = gx * gx + gz * gz;
        if (!(g2 > 0.01)) {
            return false;
        }
        x -= (d + kInland) * gx / g2;
        z -= (d + kInland) * gz / g2;
        p.nx = gx / std::sqrt(g2);
        p.nz = gz / std::sqrt(g2);
    }
    p.x = x;
    p.z = z;
    p.foot = foot;
    p.top = top;
    return true;
}

// The line of the face of a place, both ways from it out to half meters, while the face lasts.
inline std::vector<Point> lineOf(const coast_rock::Place& place, double half) {
    std::vector<Point> forward;
    std::vector<Point> backward;
    for (const int way : {1, -1}) {
        double x = place.x;
        double z = place.z;
        Point p{};
        for (double s = 0.0; s <= half && onFace(x, z, p); s += kStep) {
            (way > 0 ? forward : backward).push_back(p);
            x += way * -p.nz * kStep;
            z += way * p.nx * kStep;
        }
    }
    std::vector<Point> line(backward.rbegin(), backward.rend());
    line.insert(line.end(), forward.begin() + (forward.empty() ? 0 : 1), forward.end());
    for (std::size_t i = 0; i < line.size(); ++i) {
        line[i].s = kStep * static_cast<double>(i);
    }
    return line;
}

// The signed angle of the horizontal part of the normal (-gx, 1, -gz) from the face, and its
// tilt from the vertical, degrees.
inline std::array<double, 2> anglesOf(double gx, double gz, const Point& p) {
    const double along = gx * p.nz - gz * p.nx;
    const double out = -gx * p.nx - gz * p.nz;
    return {std::atan2(along, out) * kDegrees, std::atan(std::hypot(gx, gz)) * kDegrees};
}

// The part of values sampled at positions s repeating with a wavelength from low to high: the
// largest amplitude of a sinusoid fitted to them (their mean taken out).
inline double periodicOf(const std::vector<double>& s, const std::vector<double>& v, double low,
                  double high) {
    double mean = 0.0;
    for (const double e : v) {
        mean += e / static_cast<double>(v.size());
    }
    double most = 0.0;
    for (double wave = low; wave <= high; wave += 0.05 * low) {
        double c = 0.0, n = 0.0;
        for (std::size_t i = 0; i < v.size(); ++i) {
            const double phase = 2.0 * std::numbers::pi * s[i] / wave;
            c += (v[i] - mean) * std::cos(phase);
            n += (v[i] - mean) * std::sin(phase);
        }
        most = std::max(most, 2.0 * std::hypot(c, n) / static_cast<double>(v.size()));
    }
    return most;
}

inline double rmsOf(const std::vector<double>& v) {
    double sum = 0.0;
    for (const double e : v) {
        sum += e * e;
    }
    return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(v.size(), 1)));
}

// The samples of the level 0 of the horizon and their detail slopes over a box of samples.
struct Level0 {
    HorizonRect rect;
    std::vector<terrain::HorizonCell> cells;
    std::vector<terrain::DetailSlope> detail;

    // The slopes the material reads at (x, z), bilinear: of the detail texels or of the samples.
    [[nodiscard]] std::array<double, 2> slopeAt(double x, double z, bool fine) const {
        const double cell = fine ? kPitch / kRatio : kPitch;
        const std::int64_t width = rect.width() * (fine ? kRatio : 1);
        const double u = x / cell - static_cast<double>(rect.x0 * (fine ? kRatio : 1));
        const double v = z / cell - static_cast<double>(rect.z0 * (fine ? kRatio : 1));
        const auto a = static_cast<std::int64_t>(std::floor(u));
        const auto b = static_cast<std::int64_t>(std::floor(v));
        std::array<double, 2> out{};
        for (int q = 0; q < 4; ++q) {
            const std::int64_t i = a + (q & 1);
            const std::int64_t k = b + (q >> 1);
            const double fu = u - static_cast<double>(a);
            const double fv = v - static_cast<double>(b);
            const double w = ((q & 1) ? fu : 1.0 - fu) * ((q >> 1) ? fv : 1.0 - fv);
            const auto at = static_cast<std::size_t>(k * width + i);
            out[0] += w * (fine ? detail[at].gradX : cells[at].gradX);
            out[1] += w * (fine ? detail[at].gradZ : cells[at].gradZ);
        }
        return out;
    }
};

inline Level0 level0Around(const std::vector<Point>& line) {
    static const terrain::Ecology ecology(terrain::kDefaultWorldSeed);
    static const terrain::HorizonParams params{};
    static const terrain::HorizonSampler samples(field(), ecology, params);
    static const terrain::HorizonNormalSampler normals(field(), params, kRatio);
    double x0 = 1e18, x1 = -1e18, z0 = 1e18, z1 = -1e18;
    for (const Point& p : line) {
        x0 = std::min(x0, p.x);
        x1 = std::max(x1, p.x);
        z0 = std::min(z0, p.z);
        z1 = std::max(z1, p.z);
    }
    const auto lo = [](double v) { return static_cast<std::int64_t>(std::floor(v / kPitch)) - 6; };
    Level0 l;
    l.rect = {lo(x0), lo(z0), lo(x1) + 13, lo(z1) + 13};
    l.cells = samples.sample(0, l.rect);
    l.detail = normals.sample(0, l.rect);
    return l;
}

// The normal of the face at line[i] at the scale of a lattice: the normals of the line averaged
// over a step of it on either side (weighted down to 0 there).
inline Point atScale(const std::vector<Point>& line, std::size_t i, double lattice) {
    Point out = line[i];
    double nx = 0.0;
    double nz = 0.0;
    for (const Point& q : line) {
        const double w = 1.0 - std::abs(q.s - out.s) / lattice;
        nx += w > 0.0 ? w * q.nx : 0.0;
        nz += w > 0.0 ? w * q.nz : 0.0;
    }
    const double n = std::hypot(nx, nz);
    out.nx = nx / n;
    out.nz = nz / n;
    return out;
}

} // namespace coast_render

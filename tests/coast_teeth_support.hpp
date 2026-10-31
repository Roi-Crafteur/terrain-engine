#pragma once

// The teeth of the faces in the meshes of the tiles (agent 33, coast_patterns_test): the error of
// the normals of the vertices of a face to the normals of its ground, and the part of it repeating
// at the pitch of the cells along the contours of the face (the light and dark triangles of a
// face meshed too steep for its cells, and its crenellated top).

#include "terrain/meshing/mesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <map>
#include <numbers>
#include <vector>

namespace coast_teeth {

// Clean: an RMS error of the normals of at most kCleanRms degrees, its 95th percentile at most
// kCleanP95, its part repeating at the pitch of the cells at most kPeriodicMost.
inline constexpr double kCleanRms = 5.0;
inline constexpr double kCleanP95 = 15.0;
inline constexpr double kPeriodicMost = 2.5;
inline constexpr double kDegrees = 180.0 / std::numbers::pi;

// The gradient of a ground at (x, z).
using Gradient = std::function<std::array<double, 2>(double x, double z)>;

// A vertex of a face: where it stands, the horizontal direction up its slope, and the error of
// its normal along the face and across it (in the plane of the slope), degrees.
struct Vertex {
    double x;
    double z;
    double upX;
    double upZ;
    double along;
    double across;
};

// The errors of the normals of the vertices of the faces, and their contours: the vertices on
// each level of the lattice.
struct Teeth {
    std::vector<double> errors;
    std::map<long long, std::vector<Vertex>> contours;
};

// Adds the vertices of mesh m whose ground is at least least steep: positions origin + scale *
// vertex, on the lattice of cells of cell meters.
inline void addMesh(Teeth& t, const terrain::Mesh& m, const std::array<double, 3>& origin,
                    double scale, double cell, const Gradient& gradient, double least) {
    for (std::size_t v = 0; v < m.vertices.size(); ++v) {
        const terrain::Vec3f& n = m.normals[v];
        if (std::hypot(n.x, n.z) < 0.5 * least * n.y) {
            continue; // far gentler than a face: its gradient is not read
        }
        const double x = origin[0] + scale * m.vertices[v].x;
        const double y = origin[1] + scale * m.vertices[v].y;
        const double z = origin[2] + scale * m.vertices[v].z;
        const auto [gx, gz] = gradient(x, z);
        const double slope = std::hypot(gx, gz);
        if (slope < least) {
            continue;
        }
        const double ux = gx / slope;
        const double uz = gz / slope;
        const double length = std::sqrt(static_cast<double>(n.x * n.x + n.y * n.y + n.z * n.z));
        const double nx = n.x / length;
        const double ny = n.y / length;
        const double nz = n.z / length;
        const double along = std::asin(std::clamp(-uz * nx + ux * nz, -1.0, 1.0)) * kDegrees;
        const double across =
            (std::atan2(-(nx * ux + nz * uz), ny) - std::atan2(slope, 1.0)) * kDegrees;
        const double dot = (-gx * nx + ny - gz * nz) / std::sqrt(1.0 + slope * slope);
        t.errors.push_back(std::acos(std::clamp(dot, -1.0, 1.0)) * kDegrees);
        const double level = y / cell;
        if (std::abs(level - std::round(level)) < 1e-3) {
            t.contours[std::llround(level)].push_back({x, z, ux, uz, along, across});
        }
    }
}

struct Errors {
    double rms = 0.0;
    double p95 = 0.0;
    double periodic = 0.0;
};

// The part of the errors repeating at the pitch of the cells: along each contour (its vertices
// at their position along it), the amplitude of the errors at the wavelengths from 0.9 to 4
// cells, the power averaged over the contours, its highest.
inline double periodicOf(const Teeth& t, double cell) {
    constexpr int kFrequencies = 56;
    std::array<double, kFrequencies> along{};
    std::array<double, kFrequencies> across{};
    int count = 0;
    for (const auto& [level, c] : t.contours) {
        double ux = 0.0;
        double uz = 0.0;
        double meanAlong = 0.0;
        double meanAcross = 0.0;
        for (const Vertex& v : c) {
            ux += v.upX;
            uz += v.upZ;
            meanAlong += v.along / static_cast<double>(c.size());
            meanAcross += v.across / static_cast<double>(c.size());
        }
        std::vector<double> s;
        for (const Vertex& v : c) {
            s.push_back((-uz * v.x + ux * v.z) / std::hypot(ux, uz));
        }
        const auto [low, high] = std::minmax_element(s.begin(), s.end());
        if (c.size() < 24 || *high - *low < 8.0 * cell) {
            continue;
        }
        ++count;
        for (int k = 0; k < kFrequencies; ++k) {
            const double f = (0.25 + k / 64.0) / cell;
            std::array<double, 4> sum{};
            for (std::size_t j = 0; j < c.size(); ++j) {
                const double phase = 2.0 * std::numbers::pi * f * s[j];
                sum[0] += (c[j].along - meanAlong) * std::cos(phase);
                sum[1] += (c[j].along - meanAlong) * std::sin(phase);
                sum[2] += (c[j].across - meanAcross) * std::cos(phase);
                sum[3] += (c[j].across - meanAcross) * std::sin(phase);
            }
            const double scale = 2.0 / static_cast<double>(c.size());
            along[k] += scale * scale * (sum[0] * sum[0] + sum[1] * sum[1]);
            across[k] += scale * scale * (sum[2] * sum[2] + sum[3] * sum[3]);
        }
    }
    double most = 0.0;
    for (int k = 0; k < kFrequencies && count > 0; ++k) {
        most = std::max(most, std::sqrt(std::max(along[k], across[k]) / count));
    }
    return most;
}

inline Errors errorsOf(Teeth t, double cell) {
    Errors e;
    if (t.errors.empty()) {
        return e;
    }
    std::sort(t.errors.begin(), t.errors.end());
    double sum = 0.0;
    for (const double v : t.errors) {
        sum += v * v;
    }
    e.rms = std::sqrt(sum / static_cast<double>(t.errors.size()));
    e.p95 = t.errors[t.errors.size() * 95 / 100];
    e.periodic = periodicOf(t, cell);
    return e;
}

inline bool clean(const Errors& e) {
    return e.rms <= kCleanRms && e.p95 <= kCleanP95 && e.periodic <= kPeriodicMost;
}

} // namespace coast_teeth

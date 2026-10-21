#pragma once

// Helpers shared by the tests of the tectonic plate layer.

#include "terrain/generation/noise.hpp"
#include "terrain/generation/voronoi.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

namespace voronoi_test {

using terrain::Plate;
using terrain::TectonicPlates;

// Pseudo-random coordinates in [-range, range).
class Points {
public:
    explicit Points(std::uint64_t seed) : state_(seed) {}

    double next(double range) {
        state_ = terrain::noise::deriveSeed(state_, 0);
        const double unit = static_cast<double>(state_ >> 11) / 9007199254740992.0; // 2^53
        return (2.0 * unit - 1.0) * range;
    }

private:
    std::uint64_t state_;
};

inline double wrap(double delta, double period) {
    return delta - period * std::round(delta / period);
}

// Signed distance from a point of the warped plane to the bisector of plates a and b (their
// nearest images): negative on the side of a.
inline double signedBisectorDistance(const TectonicPlates& plates, TectonicPlates::Point q, int a,
                                     int b) {
    const terrain::WorldSize& world = plates.world();
    const Plate& pa = plates.plates()[static_cast<std::size_t>(a)];
    const Plate& pb = plates.plates()[static_cast<std::size_t>(b)];
    const double ax = wrap(pa.x - q.x, world.x);
    const double az = wrap(pa.z - q.z, world.z);
    const double bx = wrap(pb.x - q.x, world.x);
    const double bz = wrap(pb.z - q.z, world.z);
    return ((ax * ax + az * az) - (bx * bx + bz * bz)) / (2.0 * std::hypot(ax - bx, az - bz));
}

inline double bisectorDistance(const TectonicPlates& plates, TectonicPlates::Point q, int a,
                               int b) {
    return std::abs(signedBisectorDistance(plates, q, a, b));
}

// Distance from a warped point to the bisectors of a and b with every other plate (only those
// of a when b < 0): how far the point is from any third plate.
inline double thirdPlateDistance(const TectonicPlates& plates, TectonicPlates::Point q, int a,
                                 int b) {
    constexpr double kFar = std::numeric_limits<double>::infinity();
    double nearest = kFar;
    for (int c = 0; c < static_cast<int>(plates.plates().size()); ++c) {
        if (c != a && c != b) {
            nearest = std::min({nearest, bisectorDistance(plates, q, a, c),
                                b >= 0 ? bisectorDistance(plates, q, b, c) : kFar});
        }
    }
    return nearest;
}

// Nearest plate after warping, as sampleAt finds it, without the rest of the sample.
inline int plateAt(const TectonicPlates& plates, double x, double z) {
    const TectonicPlates::Point q = plates.warp(x, z);
    return plates.rawPlateAt(q.x, q.z);
}

// A boundary crossing on a segment: the last point on the side of plate a, within 1 cm of
// the first point on the side of plate b, along the unit direction (dx, dz).
struct Crossing {
    double x;
    double z;
    double dx;
    double dz;
    int a;
    int b;
};

// Bisects [t0, t1] on the line (x, z) + t (dx, dz), where the plate changes, down to 1 cm.
inline Crossing bisect(const TectonicPlates& plates, double x, double z, double dx, double dz,
                       double t0, double t1) {
    const int a = plateAt(plates, x + t0 * dx, z + t0 * dz);
    while (t1 - t0 > 0.01) {
        const double mid = 0.5 * (t0 + t1);
        (plateAt(plates, x + mid * dx, z + mid * dz) == a ? t0 : t1) = mid;
    }
    return {x + t0 * dx, z + t0 * dz, dx, dz, a, plateAt(plates, x + t1 * dx, z + t1 * dz)};
}

// Crossings of `segments` random segments of `length` meters, sampled every `step` meters.
inline std::vector<Crossing> findCrossings(const TectonicPlates& plates, std::uint64_t seed,
                                           int segments, double length, double step) {
    Points points(seed);
    std::vector<Crossing> crossings;
    for (int s = 0; s < segments; ++s) {
        const double x = points.next(plates.world().x);
        const double z = points.next(plates.world().z);
        const double angle = std::numbers::pi * points.next(1.0);
        const double dx = std::cos(angle);
        const double dz = std::sin(angle);
        int previous = plateAt(plates, x, z);
        for (double t = step; t <= length; t += step) {
            const int plate = plateAt(plates, x + t * dx, z + t * dz);
            if (plate != previous) {
                crossings.push_back(bisect(plates, x, z, dx, dz, t - step, t));
            }
            previous = plate;
        }
    }
    return crossings;
}

} // namespace voronoi_test

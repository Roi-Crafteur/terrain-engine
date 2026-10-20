#pragma once

// Outlines of tile meshes on a vertical plane, shared by the tests of the seams between tiles.

#include "terrain/streaming/stream_manager.hpp"

#include "streaming_test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace seam_test {

// A point of a vertical plane: u along the plane, y up.
struct FacePoint {
    double u = 0.0;
    double y = 0.0;
};

// The vertical plane where the world coordinate along x (alongX) or z is plane.
struct Plane {
    bool alongX = true;
    double at = 0.0;
};

inline bool onPlane(const streaming_test::WorldPoint& p, Plane plane) {
    return (plane.alongX ? p.x : p.z) == plane.at;
}

inline FacePoint project(const streaming_test::WorldPoint& p, Plane plane) {
    return {plane.alongX ? p.z : p.x, p.y};
}

inline std::vector<FacePoint> facePoints(const terrain::TileData& data, Plane plane) {
    std::vector<FacePoint> points;
    for (std::size_t i = 0; i < data.mesh.vertices.size(); ++i) {
        const streaming_test::WorldPoint p = streaming_test::worldVertex(data, i);
        if (onPlane(p, plane)) {
            points.push_back(project(p, plane));
        }
    }
    return points;
}

using Segment = std::pair<FacePoint, FacePoint>;

// Edges of the triangles lying in the plane: the outline of the mesh on it.
inline std::vector<Segment> faceSegments(const terrain::TileData& data, Plane plane) {
    std::vector<Segment> segments;
    const std::vector<std::uint32_t>& indices = data.mesh.indices;
    for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
        for (std::size_t e = 0; e < 3; ++e) {
            const auto a = streaming_test::worldVertex(data, indices[t + e]);
            const auto b = streaming_test::worldVertex(data, indices[t + (e + 1) % 3]);
            if (onPlane(a, plane) && onPlane(b, plane)) {
                segments.emplace_back(project(a, plane), project(b, plane));
            }
        }
    }
    return segments;
}

inline double distanceToSegment(FacePoint p, const Segment& s) {
    const double du = s.second.u - s.first.u;
    const double dy = s.second.y - s.first.y;
    const double length2 = du * du + dy * dy;
    double t = 0.0;
    if (length2 > 0.0) {
        t = std::clamp(((p.u - s.first.u) * du + (p.y - s.first.y) * dy) / length2, 0.0, 1.0);
    }
    return std::hypot(p.u - (s.first.u + t * du), p.y - (s.first.y + t * dy));
}

// Distance from p to the nearest segment of the outline.
inline double distanceToOutline(FacePoint p, const std::vector<Segment>& outline) {
    double nearest = std::numeric_limits<double>::infinity();
    for (const Segment& s : outline) {
        nearest = std::min(nearest, distanceToSegment(p, s));
    }
    return nearest;
}

// Largest distance from a point of `from` on the plane, with u in [uMin, uMax], to the outline
// of `to` on it. Also counts the points measured.
inline double largestGap(const terrain::TileData& from, const terrain::TileData& to, Plane plane,
                         double uMin, double uMax, std::size_t& points) {
    const std::vector<Segment> outline = faceSegments(to, plane);
    double largest = 0.0;
    points = 0;
    for (const FacePoint p : facePoints(from, plane)) {
        if (p.u < uMin || p.u > uMax) {
            continue;
        }
        largest = std::max(largest, distanceToOutline(p, outline));
        ++points;
    }
    return largest;
}

} // namespace seam_test

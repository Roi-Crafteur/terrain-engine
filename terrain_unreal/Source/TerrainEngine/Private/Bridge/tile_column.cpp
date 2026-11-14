#include "tile_column.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>

namespace terrain::unreal {

namespace {

// Barycentric coordinates are kept within this of the triangle: a point on a shared edge is in
// both triangles, never in neither.
constexpr double kEdgeTolerance = 1e-9;

// The heights of the triangles crossing the vertical line through (x, z), in their order.
template <typename Index>
std::vector<double> crossings(std::span<const CorePoint> vertices,
                              std::span<const Index> triangles, double x, double z) {
    std::vector<double> heights;
    for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
        const CorePoint& a = vertices[static_cast<std::size_t>(triangles[t])];
        const CorePoint& b = vertices[static_cast<std::size_t>(triangles[t + 1])];
        const CorePoint& c = vertices[static_cast<std::size_t>(triangles[t + 2])];
        const double det = (b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z);
        if (std::abs(det) < 1e-12) {
            continue; // seen edge on from above: a vertical face
        }
        const double u = ((x - a.x) * (c.z - a.z) - (c.x - a.x) * (z - a.z)) / det;
        const double v = ((b.x - a.x) * (z - a.z) - (x - a.x) * (b.z - a.z)) / det;
        if (u < -kEdgeTolerance || v < -kEdgeTolerance || u + v > 1.0 + kEdgeTolerance) {
            continue;
        }
        heights.push_back(a.y + u * (b.y - a.y) + v * (c.y - a.y));
    }
    return heights;
}

template <typename Index>
std::optional<double> topOf(std::span<const CorePoint> vertices, std::span<const Index> triangles,
                            double x, double z) {
    const std::vector<double> heights = crossings(vertices, triangles, x, z);
    if (heights.empty()) {
        return std::nullopt;
    }
    return *std::max_element(heights.begin(), heights.end());
}

} // namespace

std::optional<double> surfaceTop(std::span<const CorePoint> vertices,
                                 std::span<const std::uint32_t> triangles, double x, double z) {
    return topOf(vertices, triangles, x, z);
}

std::optional<double> surfaceTop(std::span<const CorePoint> vertices,
                                 std::span<const std::int32_t> triangles, double x, double z) {
    return topOf(vertices, triangles, x, z);
}

std::vector<double> surfaceHeights(std::span<const CorePoint> vertices,
                                   std::span<const std::uint32_t> triangles, double x, double z) {
    std::vector<double> heights = crossings(vertices, triangles, x, z);
    std::sort(heights.begin(), heights.end(), std::greater<>());
    std::vector<double> distinct;
    for (const double h : heights) {
        if (distinct.empty() || distinct.back() - h > 0.001) {
            distinct.push_back(h);
        }
    }
    return distinct;
}

std::vector<CorePoint> coreVertices(const TileData& data) {
    std::vector<CorePoint> points;
    points.reserve(data.mesh.vertices.size());
    for (const Vec3f& v : data.mesh.vertices) {
        points.push_back({data.originX + v.x, data.originY + v.y, data.originZ + v.z});
    }
    return points;
}

std::vector<CorePoint> coreVertices(const UnrealFrame& frame, const TileUpload& upload) {
    std::vector<CorePoint> points;
    points.reserve(upload.mesh.positions.size());
    for (const UnrealVector3f& p : upload.mesh.positions) {
        points.push_back(toCore(frame, {upload.origin.x + p.x, upload.origin.y + p.y,
                                        upload.origin.z + p.z}));
    }
    return points;
}

} // namespace terrain::unreal

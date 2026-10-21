#include "terrain/generation/geography.hpp"

#include "geography_stages.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace terrain {

namespace {

// The floors of the valleys are halfway between valleyFloor and valleyFloorLarge wide where they
// drain kHalfArea (200,000 km2), and widen with it.
constexpr double kHalfArea = 2.0e11;

// The point of the segment from a to b nearest to (px, pz): its squared distance and its
// parameter along the segment, in [0, 1].
struct Nearest {
    double distance2;
    double t;
};

Nearest nearestOnSegment(double px, double pz, double ax, double az, double bx, double bz) {
    const double ux = bx - ax;
    const double uz = bz - az;
    const double length2 = ux * ux + uz * uz;
    const double t =
        length2 > 0.0 ? std::clamp(((px - ax) * ux + (pz - az) * uz) / length2, 0.0, 1.0) : 0.0;
    const double dx = px - ax - t * ux;
    const double dz = pz - az - t * uz;
    return {dx * dx + dz * dz, t};
}

// Nodes within this many columns and rows of the point carry the valleys that may reach it.
constexpr int kWindow = 2;

// The polyline of a valley: start, middle, end.
struct Edge {
    double sx;
    double sz;
    double mx;
    double mz;
    double ex;
    double ez;

    // Squared distance from (px, pz) to the box around the polyline: at most that to it.
    [[nodiscard]] double boxDistance2(double px, double pz) const {
        const double dx = std::max({std::min({sx, mx, ex}) - px, 0.0, px - std::max({sx, mx, ex})});
        const double dz = std::max({std::min({sz, mz, ez}) - pz, 0.0, pz - std::max({sz, mz, ez})});
        return dx * dx + dz * dz;
    }
};

// The shares left by the valley of an edge at (px, pz), into shape: the larger the area drained,
// the wider the floor, whose scale goes from that of the node (scale0) to that of the node
// downstream (scale1) along the valley.
void addValley(const GeographyParams& p, const Edge& e, double px, double pz, double reach,
               double area, float scale0, float scale1, ValleyShape& shape) {
    const Nearest first = nearestOnSegment(px, pz, e.sx, e.sz, e.mx, e.mz);
    const Nearest second = nearestOnSegment(px, pz, e.mx, e.mz, e.ex, e.ez);
    const bool near = first.distance2 <= second.distance2;
    const double d2 = near ? first.distance2 : second.distance2;
    if (d2 >= reach * reach) {
        return;
    }
    const double large = area / (area + kHalfArea);
    const double t = near ? 0.5 * first.t : 0.5 + 0.5 * second.t;
    const double floor = 0.5 * (scale0 + (scale1 - scale0) * t) *
                         (p.valleyFloor + (p.valleyFloorLarge - p.valleyFloor) * large);
    const double beyond = std::max(0.0, std::sqrt(d2) - floor);
    // Walls concave at their foot and rounded at their top.
    const auto wall = [beyond](double length) {
        const double u = std::min(1.0, beyond / length);
        return static_cast<float>(u * u * (3.0 - 2.0 * u));
    };
    shape.hills = std::min(shape.hills, wall(p.valleyWall));
    shape.mountains = std::min(shape.mountains, wall(p.mountainWall));
}

} // namespace

bool Geography::downstream(int i, int j, int& di, int& dj) const {
    const std::size_t k =
        static_cast<std::size_t>((j % rows_ + rows_) % rows_) * static_cast<std::size_t>(columns_) +
        static_cast<std::size_t>((i % columns_ + columns_) % columns_);
    if (down_[k] < 0) {
        return false;
    }
    di = geography::kSteps[static_cast<std::size_t>(down_[k])][0];
    dj = geography::kSteps[static_cast<std::size_t>(down_[k])][1];
    return true;
}

float Geography::drainedArea(int i, int j) const {
    return drainedArea_[static_cast<std::size_t>((j % rows_ + rows_) % rows_) *
                            static_cast<std::size_t>(columns_) +
                        static_cast<std::size_t>((i % columns_ + columns_) % columns_)];
}

ValleyShape Geography::valleyShape(double x, double z) const {
    const GeographyParams& p = params_;
    const std::array<float, 2> warp =
        noises_->twinValleys
            ? noises_->valleyX.fbm2DPair(noises_->valleyZ, x, z)
            : std::array<float, 2>{noises_->valleyX.fbm2D(x, z), noises_->valleyZ.fbm2D(x, z)};
    // In the frame of the map, unwrapped around the point: node (i, j) at (originX + i sizeX,
    // originZ + j sizeZ) for any i and j.
    const double originX = -0.5 * columns_ * sizeX_;
    const double originZ = -0.5 * rows_ * sizeZ_;
    const double px = wrapCoordinate(x + anchorX_, world_.x) + p.valleyWarp * warp[0];
    const double pz = z + p.valleyWarp * warp[1];
    const int ci = static_cast<int>(std::floor((px - originX) / sizeX_));
    const int cj = static_cast<int>(std::floor((pz - originZ) / sizeZ_));
    // Valleys farther than this leave the relief whole.
    const double reach = 0.6 * p.valleyFloorLarge + std::max(p.valleyWall, p.mountainWall);
    // The wrapped columns and rows from ci - kWindow - 1 to ci + kWindow + 1.
    std::array<std::size_t, 2 * kWindow + 3> columns{};
    std::array<std::size_t, 2 * kWindow + 3> rows{};
    for (int k = 0; k < 2 * kWindow + 3; ++k) {
        const int i = ci - kWindow - 1 + k;
        const int j = cj - kWindow - 1 + k;
        columns[static_cast<std::size_t>(k)] =
            static_cast<std::size_t>((i % columns_ + columns_) % columns_);
        rows[static_cast<std::size_t>(k)] =
            static_cast<std::size_t>((j % rows_ + rows_) % rows_) *
            static_cast<std::size_t>(columns_);
    }
    ValleyShape shape;
    for (int b = 1; b <= 2 * kWindow + 1; ++b) {
        for (int a = 1; a <= 2 * kWindow + 1; ++a) {
            const std::size_t k = rows[static_cast<std::size_t>(b)] +
                                  columns[static_cast<std::size_t>(a)];
            if (down_[k] < 0) {
                continue;
            }
            const auto& [di, dj] = geography::kSteps[static_cast<std::size_t>(down_[k])];
            const float* from = &valleyGeometry_[geography::kValleyFloats * k];
            const float* to = &valleyGeometry_[geography::kValleyFloats *
                                               (rows[static_cast<std::size_t>(b + dj)] +
                                                columns[static_cast<std::size_t>(a + di)])];
            const double nx = originX + (ci - kWindow - 1 + a) * sizeX_;
            const double nz = originZ + (cj - kWindow - 1 + b) * sizeZ_;
            const Edge edge{nx + from[0],          nz + from[1],
                            nx + from[2],          nz + from[3],
                            nx + di * sizeX_ + to[0], nz + dj * sizeZ_ + to[1]};
            if (edge.boxDistance2(px, pz) >= reach * reach) {
                continue;
            }
            addValley(p, edge, px, pz, reach, drainedArea_[k], from[4], to[4], shape);
        }
    }
    return shape;
}

} // namespace terrain

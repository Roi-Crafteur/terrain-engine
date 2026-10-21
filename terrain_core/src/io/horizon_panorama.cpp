#include "terrain/io/horizon_panorama.hpp"

#include "terrain/det_math.hpp"
#include "terrain/io/ecology_png.hpp"
#include "terrain/io/horizon_png.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace terrain {

namespace {

constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr Rgb8 kZenithRgb = {70, 120, 200};
constexpr Rgb8 kHazeRgb = {200, 215, 235};

// The next distance along a ray: steps of 2 m or distance / 256, the larger, finer than the
// pitch of the level holding the point (at most 1/64 of the distance).
double nextDistance(double distance) { return distance + std::max(2.0, distance / 256.0); }

struct Ray {
    double dx;
    double dz;
};

Ray rayOf(const PanoramaParams& params, int column) {
    const double azimuth = columnAzimuth(params, column);
    return {dmath::sinTurns(azimuth), -dmath::cosTurns(azimuth)};
}

// Elevation tangent of a point of the ground seen from the eye, the drop of the curvature
// included. The sea surface hides the ground under it.
double elevationTangent(const PanoramaParams& params, double distance, float ground) {
    const double surface = std::max(ground, kSeaLevel);
    const double drop = distance * distance / (2.0 * params.planetRadius);
    return (surface - drop - params.eyeHeight) / distance;
}

Rgb8 mix(const Rgb8& a, const Rgb8& b, double t) {
    Rgb8 color{};
    for (std::size_t c = 0; c < 3; ++c) {
        color[c] =
            static_cast<std::uint8_t>(std::clamp(a[c] + (b[c] - a[c]) * t + 0.5, 0.0, 255.0));
    }
    return color;
}

// Color of the ground seen at this distance: the material of the nearest sample, shaded by its
// gradient (lit from the north-west and above), then hazed.
Rgb8 groundColor(const PanoramaParams& params, const HorizonPoint& point, double distance) {
    Rgb8 color = kMaterialRgb[static_cast<std::size_t>(SurfaceMaterial::Water)];
    const bool water = point.height < kSeaLevel || point.nearest == nullptr ||
                       (point.nearest->flags & kHorizonWater) != 0;
    if (!water) {
        const HorizonCell& cell = *point.nearest;
        const double nx = -cell.gradX;
        const double nz = -cell.gradZ;
        const double light = std::clamp(
            (-nx + 1.0 - nz) / (std::sqrt(3.0) * std::sqrt(nx * nx + 1.0 + nz * nz)), 0.25, 1.0);
        color = mix({0, 0, 0}, kMaterialRgb[static_cast<std::size_t>(cell.material)], light);
    }
    return mix(color, kHazeRgb, 1.0 - dmath::exp(-distance / params.hazeDistance));
}

void setPixel(Image& image, int column, int row, const Rgb8& color) {
    const auto at = (static_cast<std::size_t>(row) * static_cast<std::size_t>(image.width) +
                     static_cast<std::size_t>(column)) *
                    3;
    image.pixels[at] = color[0];
    image.pixels[at + 1] = color[1];
    image.pixels[at + 2] = color[2];
}

// Rows are linear in the elevation tangent: the eye level at eyeLevelRow of the height.
struct RowScale {
    double eyeRow;
    double tangentPerRow;

    [[nodiscard]] int rowOf(double tangent) const {
        return static_cast<int>(std::floor(eyeRow - tangent / tangentPerRow));
    }
};

// Casts the ray of a column over the clipmap, front to back: each point of the ground higher
// on the image than every nearer one covers the rows up to them.
void castColumn(const HorizonClipmap& clipmap, const PanoramaParams& params, const RowScale& rows,
                int column, Panorama& panorama) {
    const Ray ray = rayOf(params, column);
    int lowestFree = params.height; // rows from 0 to lowestFree - 1 still show the sky
    int level = 0;
    double& best = panorama.silhouette.tangent[static_cast<std::size_t>(column)];
    for (double d = params.minDistance; d <= params.maxDistance; d = nextDistance(d)) {
        const std::optional<HorizonPoint> point =
            horizonPointAt(clipmap, params.eyeX + ray.dx * d, params.eyeZ + ray.dz * d, level);
        if (!point) {
            break; // beyond the coarsest level
        }
        level = point->level; // the windows are nested around the eye: never finer farther
        const double tangent = elevationTangent(params, d, point->height);
        if (tangent > best) {
            best = tangent;
            panorama.silhouette.distance[static_cast<std::size_t>(column)] = d;
        }
        const int row = std::max(rows.rowOf(tangent), 0);
        if (row < lowestFree) {
            const Rgb8 color = groundColor(params, *point, d);
            for (int r = row; r < lowestFree; ++r) {
                setPixel(panorama.image, column, r, color);
            }
            lowestFree = row;
            panorama.farthest[static_cast<std::size_t>(column)] = d;
        }
    }
    for (int r = 0; r < lowestFree; ++r) {
        const double t = std::clamp(static_cast<double>(r) / rows.eyeRow, 0.0, 1.0);
        setPixel(panorama.image, column, r, mix(kZenithRgb, kHazeRgb, t * t));
    }
}

} // namespace

GroundHeight clipmapGround(const HorizonClipmap& clipmap) {
    return [&clipmap](double x, double z) -> std::optional<float> {
        const std::optional<HorizonPoint> point = horizonPointAt(clipmap, x, z);
        return point ? std::optional<float>(point->height) : std::nullopt;
    };
}

double columnAzimuth(const PanoramaParams& params, int column) {
    return (column + 0.5) / params.width;
}

Silhouette traceSilhouette(const GroundHeight& ground, const PanoramaParams& params,
                           int firstColumn, int columns, double maxDistance) {
    Silhouette silhouette;
    silhouette.tangent.assign(static_cast<std::size_t>(std::max(columns, 0)),
                              -std::numeric_limits<double>::infinity());
    silhouette.distance.assign(silhouette.tangent.size(), 0.0);
    for (int c = 0; c < columns; ++c) {
        const Ray ray = rayOf(params, firstColumn + c);
        for (double d = params.minDistance; d <= maxDistance; d = nextDistance(d)) {
            const std::optional<float> height =
                ground(params.eyeX + ray.dx * d, params.eyeZ + ray.dz * d);
            if (!height) {
                break;
            }
            const double tangent = elevationTangent(params, d, *height);
            if (tangent > silhouette.tangent[static_cast<std::size_t>(c)]) {
                silhouette.tangent[static_cast<std::size_t>(c)] = tangent;
                silhouette.distance[static_cast<std::size_t>(c)] = d;
            }
        }
    }
    return silhouette;
}

Panorama renderHorizonPanorama(const HorizonClipmap& clipmap, const PanoramaParams& params) {
    if (params.width < 2 || params.height < 2 || !(params.exaggeration > 0.0) ||
        !(params.planetRadius > 0.0) || !(params.hazeDistance > 0.0) ||
        !(params.minDistance > 0.0)) {
        throw std::invalid_argument("PanoramaParams: invalid image or scales");
    }
    if (!horizonPointAt(clipmap, params.eyeX, params.eyeZ)) {
        throw std::invalid_argument("renderHorizonPanorama: the eye is outside the levels");
    }
    Panorama panorama;
    const auto pixels =
        static_cast<std::size_t>(params.width) * static_cast<std::size_t>(params.height) * 3;
    panorama.image = Image{params.width, params.height, 3, std::vector<std::uint8_t>(pixels, 0)};
    const auto columns = static_cast<std::size_t>(params.width);
    panorama.silhouette.tangent.assign(columns, -std::numeric_limits<double>::infinity());
    panorama.silhouette.distance.assign(columns, 0.0);
    panorama.farthest.assign(columns, 0.0);
    const RowScale rows{params.eyeLevelRow * params.height,
                        kTwoPi / params.width / params.exaggeration};
    for (int column = 0; column < params.width; ++column) {
        castColumn(clipmap, params, rows, column, panorama);
    }
    return panorama;
}

} // namespace terrain

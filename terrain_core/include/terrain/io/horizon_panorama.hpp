#pragma once

#include "terrain/io/png.hpp"
#include "terrain/streaming/horizon_epoch.hpp"

#include <functional>
#include <optional>
#include <vector>

namespace terrain {

// A 360 degree panorama of the far horizon from a viewpoint: a control image of what its grids
// show, not a renderer. Rays are cast over the ground of the clipmap, with the curvature of a
// planet simulated by a drop of d^2 / 2R at distance d, a Lambert shading by the gradient, the
// color of the material and a haze growing with the distance.
struct PanoramaParams {
    double eyeX = 0.0;
    double eyeZ = 0.0;
    double eyeHeight = 0.0; // altitude of the eye, meters
    int width = 3600;       // pixels for a full turn: the first column faces north (-Z), then east
    int height = 480;
    // Vertical angles are drawn this many times larger than horizontal ones (rows are linear in
    // the tangent of the elevation angle), so that the far relief shows.
    double exaggeration = 4.0;
    double eyeLevelRow = 0.4; // share of the height above the row of the eye level
    double planetRadius = 6'371'000.0;
    double minDistance = 20.0;
    double maxDistance = 600'000.0;
    double hazeDistance = 120'000.0; // the haze covers 1 - exp(-d / hazeDistance) of the color
};

// Height of the ground at (x, z), any image, or nullopt where it is unknown.
using GroundHeight = std::function<std::optional<float>(double x, double z)>;

// The ground of a clipmap: the finest level holding each point (horizonPointAt).
[[nodiscard]] GroundHeight clipmapGround(const HorizonClipmap& clipmap);

// Azimuth of the center of a column, in turns from the north, clockwise seen from above (east
// is a quarter turn): the column faces (sin, -cos) of it along (x, z).
[[nodiscard]] double columnAzimuth(const PanoramaParams& params, int column);

// Elevation tangent of the highest point of the ground (the sea surface over the water) seen
// in each column, the drop of the curvature included, and its distance.
struct Silhouette {
    std::vector<double> tangent;  // -infinity where no ground was found
    std::vector<double> distance; // 0 where no ground was found
};

// Silhouette of the columns [firstColumn, firstColumn + columns) of the panorama, over the
// ground met from params.minDistance to maxDistance (or until the ground is unknown), in steps
// of 2 m or distance / stepDivisor, the larger.
[[nodiscard]] Silhouette traceSilhouette(const GroundHeight& ground, const PanoramaParams& params,
                                         int firstColumn, int columns, double maxDistance,
                                         double stepDivisor = 256.0);

struct Panorama {
    Image image;           // RGB, width x height
    Silhouette silhouette; // of every column, up to params.maxDistance
    // Per column, the farthest distance at which the ground covers a pixel: how far the relief
    // shows on the image. 0 where none does.
    std::vector<double> farthest;
};

// Throws std::invalid_argument for an image smaller than 2 x 2 pixels, a non-positive
// exaggeration, planet radius or haze distance, or a viewpoint outside the finest level.
[[nodiscard]] Panorama renderHorizonPanorama(const HorizonClipmap& clipmap,
                                             const PanoramaParams& params);

} // namespace terrain

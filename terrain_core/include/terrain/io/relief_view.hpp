#pragma once

#include "terrain/io/png.hpp"

#include <functional>

namespace terrain {

// A view of the relief in perspective from a point, as a camera standing there sees it: a
// control image to judge the relief at the scale of a walker or of a flight, not a renderer.
// Rectilinear projection with a horizontal field of view; the axis of the camera is horizontal
// and the image is shifted vertically by tilt (a shift lens), so that vertical lines stay
// vertical and each column is a vertical plane through the eye. One ray per column over the
// ground, front to back, with the curvature of the planet (a drop of d^2 / 2R at a distance d),
// the hypsometric colors of relief_png lit by a low sun on the left of the view, a haze growing
// with the distance, and the sea surface over the ground below it (or the water given). A scale
// mark: a tower of red and white bands of 50 m standing on the ground.
struct ViewParams {
    double eyeX = 0.0;
    double eyeZ = 0.0;
    double eyeHeight = 0.0; // altitude of the eye, meters
    // Direction of the axis in turns from the north (-Z), clockwise seen from above (east is a
    // quarter turn): the axis faces (sin, -cos) of it along (x, z).
    double heading = 0.0;
    // Tangent of the elevation of the center row of the image (the shift of the lens): above 0,
    // the image shows more of what is above the eye level.
    double tilt = 0.0;
    double fieldOfView = 70.0 / 360.0; // horizontal, turns
    int width = 1600;
    int height = 900;
    double planetRadius = 6'371'000.0;
    double minDistance = 4.0;        // depth along the axis of the first sample of a ray
    double maxDistance = 400'000.0;  // and of the last
    double hazeDistance = 90'000.0;  // the haze covers 1 - exp(-d / hazeDistance) of the color
    // The tower, when towerHeight > 0: a square towerWidth wide standing on the ground at
    // (towerX, towerZ), its top towerHeight above the ground at its center.
    double towerX = 0.0;
    double towerZ = 0.0;
    double towerHeight = 0.0;
    double towerWidth = 40.0;
};

// Height of the ground at (x, z), meters; called from several threads at once.
using ViewGround = std::function<float(double x, double z)>;
// The surface of the water at (x, z), meters: the ground is under water where it is above the
// ground, dry elsewhere. Called from several threads at once.
using ViewWater = std::function<float(double x, double z)>;

// Throws std::invalid_argument for an image smaller than 2 x 2 pixels, a field of view outside
// (0, 0.45) turns, or a non-positive planet radius, haze distance, first depth or tower width,
// or a last depth below the first. The columns are cast on up to threads threads (0: the
// hardware concurrency); the image does not depend on their number.
[[nodiscard]] Image renderReliefView(const ViewGround& ground, const ViewParams& params,
                                     int threads);
// The same view over the water given: lakes and rivers flat at their levels, and dry ground
// below the sea level (a depression) colored as land.
[[nodiscard]] Image renderReliefView(const ViewGround& ground, const ViewWater& water,
                                     const ViewParams& params, int threads);

// What the eye of params sees of the point (x, z) at altitude height: the tangent of its
// elevation (the drop of the curvature included), its distance, and whether the ground hides it.
// The line of sight is followed in steps of distance / 2,000 (at least 5 m) up to margin meters
// before the point: the ground nearer than that belongs to the point itself (its own summit).
struct Sightline {
    double tangent = 0.0;
    double distance = 0.0;
    bool visible = true;
    double blockedAt = 0.0; // distance of the first ground above the line, when hidden
};

[[nodiscard]] Sightline sightline(const ViewGround& ground, const ViewParams& params, double x,
                                  double z, double height, double margin);

} // namespace terrain

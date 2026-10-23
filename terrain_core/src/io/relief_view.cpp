#include "terrain/io/relief_view.hpp"

#include "../generation/planet/planet_parallel.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/io/relief_png.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace terrain {

namespace {

using Rgb = std::array<double, 3>;

constexpr Rgb kZenith = {70.0, 120.0, 200.0};
constexpr Rgb kHaze = {200.0, 215.0, 235.0};
constexpr Rgb kWater = {55.0, 105.0, 165.0};
constexpr Rgb kTowerRed = {205.0, 45.0, 35.0};
constexpr Rgb kTowerWhite = {240.0, 240.0, 240.0};
constexpr double kTowerBand = 50.0; // meters of each band of the tower

// The sun is low on the left of the view and a little behind the eye, so that the slopes and the
// walls facing the eye are lit across.
constexpr double kSunLeft = 0.3;     // turns left of the axis
constexpr double kSunSine = 0.5;     // of its elevation (30 degrees)
constexpr int kColumnBlock = 16;     // columns per block on the threads
// Fine enough that the rock steps of the walls are not missed from one column to the next (no
// comb of stripes on the faces).
constexpr double kStepShare = 0.0005; // step along a ray: this share of its depth, at least
constexpr double kMinStep = 0.25;    // this many meters
// Where the ground rises more than kFaceSlope meters per meter of the ray between two points (70
// degrees), the rows between them show the face of a wall, lit as such (not a blend of their
// colors).
constexpr double kFaceSlope = 2.75;
constexpr double kFaceRead = 4.0;
constexpr Rgb kRock = {128.0, 118.0, 108.0};
// The ground turns to bare rock as its slope passes kRockSlopeLow to kRockSlopeHigh.
constexpr double kRockSlopeLow = 0.7;
constexpr double kRockSlopeHigh = 1.0;

// The light on ground whose normal makes this cosine with the sun: an ambient share, so that the
// faces in the shade keep their relief.
double light(double lambert) {
    return std::clamp(0.45 + 0.75 * lambert, 0.3, 1.2);
}

Rgb mix(const Rgb& a, const Rgb& b, double t) {
    return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
}

Rgb scale(const Rgb& a, double factor) {
    return {a[0] * factor, a[1] * factor, a[2] * factor};
}

std::uint8_t toByte(double value) {
    return static_cast<std::uint8_t>(std::clamp(value + 0.5, 0.0, 255.0));
}

void setPixel(Image& image, int column, int row, const Rgb& color) {
    const auto at = (static_cast<std::size_t>(row) * static_cast<std::size_t>(image.width) +
                     static_cast<std::size_t>(column)) *
                    3;
    image.pixels[at] = toByte(color[0]);
    image.pixels[at + 1] = toByte(color[1]);
    image.pixels[at + 2] = toByte(color[2]);
}

// The frame of the camera: its axis and its right along (x, z), the focal length in pixels and
// the row of the elevation tangent 0.
struct Camera {
    double forwardX;
    double forwardZ;
    double rightX;
    double rightZ;
    double focal;
    double centerRow;
    std::array<double, 3> sun; // unit vector toward the sun (x, up, z)

    [[nodiscard]] int rowOf(double tangent) const {
        return static_cast<int>(std::floor(centerRow - focal * tangent));
    }
};

Camera cameraOf(const ViewParams& p) {
    Camera c{};
    c.forwardX = dmath::sinTurns(p.heading);
    c.forwardZ = -dmath::cosTurns(p.heading);
    c.rightX = dmath::cosTurns(p.heading);
    c.rightZ = dmath::sinTurns(p.heading);
    const double half = 0.5 * p.fieldOfView;
    c.focal = 0.5 * p.width * dmath::cosTurns(half) / dmath::sinTurns(half);
    c.centerRow = 0.5 * p.height + c.focal * p.tilt;
    const double cosine = std::sqrt(1.0 - kSunSine * kSunSine);
    const double azimuth = p.heading - kSunLeft;
    c.sun = {dmath::sinTurns(azimuth) * cosine, kSunSine, -dmath::cosTurns(azimuth) * cosine};
    return c;
}

double drop(const ViewParams& p, double distance) {
    return distance * distance / (2.0 * p.planetRadius);
}

// Color of the ground at (x, z), height h, seen from distance: the tint lit by the sun through
// the gradient of the ground over run meters on each side (centered, so that a sharp crest
// takes the mean of its faces), the water (wet) flat, then hazed.
Rgb groundColor(const ViewGround& ground, const ViewParams& p, const Camera& c, double x, double z,
                float h, bool wet, double run, double distance) {
    Rgb color = kWater;
    if (!wet) {
        const double gx =
            (static_cast<double>(ground(x + run, z)) - ground(x - run, z)) / (2.0 * run);
        const double gz =
            (static_cast<double>(ground(x, z + run)) - ground(x, z - run)) / (2.0 * run);
        const double lambert =
            (-gx * c.sun[0] + c.sun[1] - gz * c.sun[2]) / std::sqrt(gx * gx + 1.0 + gz * gz);
        const std::array<float, 3> tint = reliefColor(h, false);
        // Steep ground is bare rock, whatever its altitude.
        const double steep = std::clamp((std::sqrt(gx * gx + gz * gz) - kRockSlopeLow) /
                                            (kRockSlopeHigh - kRockSlopeLow),
                                        0.0, 1.0);
        color = scale(mix({tint[0], tint[1], tint[2]}, kRock, steep), light(lambert));
    }
    return mix(color, kHaze, 1.0 - dmath::exp(-distance / p.hazeDistance));
}

// Color of the face of a wall seen along the ray (unit direction dirX, dirZ): it rises rise
// meters over run meters away from the eye, facing it; the tint of the rock lit by the sun on
// that face, then hazed.
Rgb faceColor(const ViewParams& p, const Camera& c, double dirX, double dirZ, double run,
              double rise, double distance) {
    // Read at least as steep as kFaceRead: the walls of a scene are lit alike, not in stripes as
    // the run between two points of a ray comes and goes.
    const double g = std::max(rise / run, kFaceRead);
    const double lambert =
        (-g * dirX * c.sun[0] + c.sun[1] - g * dirZ * c.sun[2]) / std::sqrt(g * g + 1.0);
    const Rgb color = scale(kRock, light(lambert));
    return mix(color, kHaze, 1.0 - dmath::exp(-distance / p.hazeDistance));
}

struct Tower {
    bool present = false;
    double base = 0.0;
    double top = 0.0;

    [[nodiscard]] bool covers(const ViewParams& p, double x, double z) const {
        return present && std::abs(x - p.towerX) <= 0.5 * p.towerWidth &&
               std::abs(z - p.towerZ) <= 0.5 * p.towerWidth;
    }
};

// The tower seen in rows [first, last) at depth s along the axis, distance meters away: bands of
// kTowerBand meters from its base.
void paintTower(Image& image, const ViewParams& p, const Camera& c, const Tower& tower,
                int column, int first, int last, double s, double distance) {
    const double haze = 1.0 - dmath::exp(-distance / p.hazeDistance);
    for (int r = first; r < last; ++r) {
        const double above = p.eyeHeight + (c.centerRow - (r + 0.5)) * s / c.focal +
                             drop(p, distance) - tower.base;
        const bool red = static_cast<long long>(std::floor(above / kTowerBand)) % 2 == 0;
        setPixel(image, column, r, mix(red ? kTowerRed : kTowerWhite, kHaze, haze));
    }
}

void paintSky(Image& image, const Camera& c, int column, int lowestFree) {
    for (int r = 0; r < lowestFree; ++r) {
        const double t = c.centerRow > 0.0 ? std::clamp(r / c.centerRow, 0.0, 1.0) : 1.0;
        setPixel(image, column, r, mix(kZenith, kHaze, t * t));
    }
}

// Casts the ray of a column front to back: each point of the ground higher on the image than
// every nearer one covers the rows down to them, in colors going from that of the nearer point
// when it was seen (the face of ground between them) to its own.
void castColumn(const ViewGround& ground, const ViewWater& water, const ViewParams& p,
                const Camera& c, const Tower& tower, int column, Image& image) {
    const double u = (column + 0.5 - 0.5 * p.width) / c.focal;
    const double dirX = c.forwardX + u * c.rightX;
    const double dirZ = c.forwardZ + u * c.rightZ;
    const double stretch = std::sqrt(1.0 + u * u); // horizontal meters per meter of depth
    int lowestFree = p.height;                     // rows 0 to lowestFree - 1 still free
    bool seen = false; // the previous point of the ray was seen, in color below
    Rgb below{};
    double before = 0.0; // the height of the previous point of the ray
    double previous = 0.0; // and its depth
    for (double s = p.minDistance; s <= p.maxDistance && lowestFree > 0;) {
        const double x = p.eyeX + dirX * s;
        const double z = p.eyeZ + dirZ * s;
        const double distance = s * stretch;
        double step = std::max(kMinStep, kStepShare * s);
        if (tower.present &&
            dmath::hypot(x - p.towerX, z - p.towerZ) < 2.0 * p.towerWidth + step) {
            step = std::min(step, 0.25 * p.towerWidth);
        }
        const bool onTower = tower.covers(p, x, z);
        const float h = onTower ? static_cast<float>(tower.top) : ground(x, z);
        const float level = onTower ? h : water(x, z);
        const bool wet = level > h;
        const double surface = static_cast<double>(wet ? level : h);
        const int row = std::max(c.rowOf((surface - drop(p, distance) - p.eyeHeight) / s), 0);
        const bool visible = row < lowestFree;
        if (visible && onTower) {
            paintTower(image, p, c, tower, column, row, lowestFree, s, distance);
        } else if (visible) {
            const Rgb color =
                groundColor(ground, p, c, x, z, h, wet, std::max(1.0, step), distance);
            const double rows = lowestFree - row;
            const double run = (s - previous) * stretch;
            const double rise = static_cast<double>(h) - before;
            const bool face = seen && rise > kFaceSlope * run;
            const Rgb wall =
                face ? faceColor(p, c, dirX / stretch, dirZ / stretch, run, rise, distance) : color;
            for (int r = row; r < lowestFree; ++r) {
                setPixel(image, column, r,
                         face ? wall : (seen ? mix(color, below, (r - row + 0.5) / rows) : color));
            }
            below = color;
        }
        seen = visible && !onTower;
        before = static_cast<double>(h);
        previous = s;
        lowestFree = visible ? row : lowestFree;
        s += step;
    }
    paintSky(image, c, column, lowestFree);
}

void validateView(const ViewParams& p) {
    if (p.width < 2 || p.height < 2 || !(p.fieldOfView > 0.0) || !(p.fieldOfView < 0.45) ||
        !(p.planetRadius > 0.0) || !(p.hazeDistance > 0.0) || !(p.minDistance > 0.0) ||
        !(p.maxDistance >= p.minDistance) || !(p.towerWidth > 0.0) ||
        !std::isfinite(p.eyeHeight) || !std::isfinite(p.tilt)) {
        throw std::invalid_argument("ViewParams: invalid image, field of view or distances");
    }
}

} // namespace

Image renderReliefView(const ViewGround& ground, const ViewParams& params, int threads) {
    return renderReliefView(
        ground, [](double, double) { return kSeaLevel; }, params, threads);
}

Image renderReliefView(const ViewGround& ground, const ViewWater& water, const ViewParams& params,
                       int threads) {
    validateView(params);
    const Camera camera = cameraOf(params);
    Tower tower;
    if (params.towerHeight > 0.0) {
        tower.present = true;
        tower.base = std::max(static_cast<double>(ground(params.towerX, params.towerZ)),
                              static_cast<double>(water(params.towerX, params.towerZ)));
        tower.top = tower.base + params.towerHeight;
    }
    const auto pixels =
        static_cast<std::size_t>(params.width) * static_cast<std::size_t>(params.height) * 3;
    Image image{params.width, params.height, 3, std::vector<std::uint8_t>(pixels, 0)};
    planet::parallelBlocks(params.width, kColumnBlock, planet::resolveThreads(threads),
                           [&](int begin, int end) {
                               for (int column = begin; column < end; ++column) {
                                   castColumn(ground, water, params, camera, tower, column,
                                              image);
                               }
                           });
    return image;
}

Sightline sightline(const ViewGround& ground, const ViewParams& params, double x, double z,
                    double height, double margin) {
    return sightline(
        ground, [](double, double) { return kSeaLevel; }, params, x, z, height, margin);
}

Sightline sightline(const ViewGround& ground, const ViewWater& water, const ViewParams& params,
                    double x, double z, double height, double margin) {
    Sightline line;
    const double dx = x - params.eyeX;
    const double dz = z - params.eyeZ;
    line.distance = dmath::hypot(dx, dz);
    if (!(line.distance > 0.0)) {
        return line;
    }
    line.tangent = (height - drop(params, line.distance) - params.eyeHeight) / line.distance;
    const double step = std::max(5.0, line.distance / 2000.0);
    for (double d = step; d < line.distance - margin; d += step) {
        const double t = d / line.distance;
        const double px = params.eyeX + dx * t;
        const double pz = params.eyeZ + dz * t;
        const double h =
            std::max(static_cast<double>(ground(px, pz)), static_cast<double>(water(px, pz)));
        if ((h - drop(params, d) - params.eyeHeight) / d > line.tangent) {
            line.visible = false;
            line.blockedAt = d;
            break;
        }
    }
    return line;
}

} // namespace terrain

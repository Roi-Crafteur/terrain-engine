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
// Where the slope of the rising ground along the ray changes by more than kEdgeShare of kFaceSlope
// from one step to the next, the ray walks the step again in kFineSteps: the edges of a face fall
// where the ground turns, not on the steps of the ray (on a face of 6 m per meter, a step of
// 0.25 m spans 1.5 m of it: teeth along its edges). Even slopes keep their steps.
constexpr double kEdgeShare = 0.0625;
constexpr double kFineSteps = 16.0;
// Where the ground rises more than kFaceSlope meters per meter of the ray between two points (70
// degrees), the rows between them show the face of a wall, lit as such (not a blend of their
// colors).
constexpr double kFaceSlope = 2.75;
// The face fades in from kFaceSlope - kFaceBlend to kFaceSlope + kFaceBlend: no flip of colors from
// one column to the next on a wall about that steep.
constexpr double kFaceBlend = 0.5;
constexpr double kFaceRead = 4.0;
constexpr double kFaceGrad = 2.0;
constexpr Rgb kRock = {128.0, 118.0, 108.0};
constexpr Rgb kFall = {190.0, 210.0, 230.0}; // the water of a fall down a face: white water
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

// Color of the face of a wall seen along the ray (unit direction dirX, dirZ) at (x, z): the tint of
// the rock (of the water of a fall where it is wet) lit by the sun through the gradient of the
// ground over kFaceGrad meters on each side (the rock of the face, the same whatever the steps of
// the ray, no stripes), read at least as steep as kFaceRead, then hazed.
Rgb faceColor(const ViewGround& ground, const ViewParams& p, const Camera& c, double x, double z,
              double dirX, double dirZ, bool wet, double distance) {
    double gx = (static_cast<double>(ground(x + kFaceGrad, z)) - ground(x - kFaceGrad, z)) /
                (2.0 * kFaceGrad);
    double gz = (static_cast<double>(ground(x, z + kFaceGrad)) - ground(x, z - kFaceGrad)) /
                (2.0 * kFaceGrad);
    // Facing the eye: up the ray.
    if (gx * dirX + gz * dirZ < 0.0) {
        gx = -gx;
        gz = -gz;
    }
    const double g = std::sqrt(gx * gx + gz * gz);
    if (g < kFaceRead) {
        const double along = gx * dirX + gz * dirZ;
        const double fill = std::sqrt(std::max(0.0, kFaceRead * kFaceRead - g * g + along * along));
        gx += (fill - along) * dirX;
        gz += (fill - along) * dirZ;
    }
    const double lambert = (-gx * c.sun[0] + c.sun[1] - gz * c.sun[2]) /
                           std::sqrt(gx * gx + gz * gz + 1.0);
    const Rgb color = scale(wet ? kFall : kRock, light(lambert));
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

// The step of the ray from depth s: a share of the depth, shorter in the fine steps and by the
// tower.
double stepAt(const ViewParams& p, const Tower& tower, double x, double z, double s, bool fine) {
    double step = std::max(kMinStep, kStepShare * s) / (fine ? kFineSteps : 1.0);
    if (tower.present && dmath::hypot(x - p.towerX, z - p.towerZ) < 2.0 * p.towerWidth + step) {
        step = std::min(step, 0.25 * p.towerWidth);
    }
    return step;
}

// The slope along the ray changes at an edge of a face: from gentle to steep, or back.
bool slopeBreaks(double slope, double next) {
    const double edge = kEdgeShare * kFaceSlope;
    return std::abs(next - slope) > edge && std::max(next, slope) > edge;
}

// A ray of the view: its column and its direction, horizontal.
struct Ray {
    int column = 0;
    double dirX = 0.0;    // per meter of depth along the axis of the view
    double dirZ = 0.0;
    double stretch = 1.0; // horizontal meters per meter of depth
};

// A point of a ray.
struct RayPoint {
    double x = 0.0;
    double z = 0.0;
    double depth = 0.0;   // along the axis of the view
    double height = 0.0;  // of the ground
    double surface = 0.0; // what is drawn there: the ground, or the water over it
    bool wet = false;
    int row = 0;          // of the image where it is seen (below it, maybe)
};

// Paints the rows of the point at from its row down to lowestFree: as much of a face from the
// point before as the ground between them is one (its colors at both ends of the step, blended
// down to the row of the point before: the same shading whatever the length of the step), the
// rest a blend from the color below of the point before where it was painted. Returns the color
// of the point.
Rgb paintGround(const ViewGround& ground, const ViewParams& p, const Camera& c, const Ray& ray,
                const RayPoint& at, const RayPoint& before, double face, bool painted,
                const Rgb& below, double step, int lowestFree, Image& image) {
    const double distance = at.depth * ray.stretch;
    const Rgb color = groundColor(ground, p, c, at.x, at.z, static_cast<float>(at.height), at.wet,
                                  std::max(1.0, step), distance);
    const double ux = ray.dirX / ray.stretch;
    const double uz = ray.dirZ / ray.stretch;
    Rgb top = color;
    Rgb foot = painted ? below : color;
    if (face > 0.0) {
        top = mix(top, faceColor(ground, p, c, at.x, at.z, ux, uz, at.wet, distance), face);
        foot = mix(foot, faceColor(ground, p, c, before.x, before.z, ux, uz, at.wet, distance),
                   face);
    }
    const double rows = std::max(before.row, lowestFree) - at.row;
    for (int r = at.row; r < lowestFree; ++r) {
        setPixel(image, ray.column, r, mix(top, foot, (r - at.row + 0.5) / rows));
    }
    return color;
}

// Casts the ray of a column front to back: each point of the ground higher on the image than
// every nearer one covers the rows down to them, in colors going from that of the nearer point
// when it was seen (the face of ground between them) to its own.
void castColumn(const ViewGround& ground, const ViewWater& water, const ViewParams& p,
                const Camera& c, const Tower& tower, int column, Image& image) {
    const double u = (column + 0.5 - 0.5 * p.width) / c.focal;
    const Ray ray{column, c.forwardX + u * c.rightX, c.forwardZ + u * c.rightZ,
                  std::sqrt(1.0 + u * u)};
    int lowestFree = p.height; // rows 0 to lowestFree - 1 still free
    RayPoint before;           // the previous point of the ray
    before.row = p.height;
    bool fromGround = false; // it is a point of the ground (not the first, not on the tower)
    bool seen = false;    // it was seen, or lies below the image
    bool painted = false; // it was seen, in color below
    Rgb below{};
    double slope = 0.0;        // of the ground along the ray up to it
    double surfaceSlope = 0.0; // and of what is drawn
    double fineUntil = 0.0;    // the ray walks in fine steps up to this depth
    bool coarse = false;       // the point was reached by a coarse step
    for (double s = p.minDistance; s <= p.maxDistance && lowestFree > 0;) {
        RayPoint at{p.eyeX + ray.dirX * s, p.eyeZ + ray.dirZ * s, s, 0.0, 0.0, false, 0};
        const double distance = s * ray.stretch;
        const bool fine = s < fineUntil;
        const double step = stepAt(p, tower, at.x, at.z, s, fine);
        const bool onTower = tower.covers(p, at.x, at.z);
        const float h = onTower ? static_cast<float>(tower.top) : ground(at.x, at.z);
        const float level = onTower ? h : water(at.x, at.z);
        at.height = static_cast<double>(h);
        at.wet = level > h;
        at.surface = static_cast<double>(at.wet ? level : h);
        at.row = std::max(c.rowOf((at.surface - drop(p, distance) - p.eyeHeight) / s), 0);
        const bool visible = at.row < lowestFree;
        const double run = (s - before.depth) * ray.stretch;
        const double rise = at.height - before.height;
        // A slope that breaks on a step seen: walked again in fine steps.
        if (coarse && fromGround && !onTower && (seen || visible) &&
            (slopeBreaks(slope, rise / run) ||
             slopeBreaks(surfaceSlope, (at.surface - before.surface) / run))) {
            fineUntil = s;
            s = before.depth + (s - before.depth) / kFineSteps;
            coarse = false;
            continue;
        }
        if (visible && onTower) {
            paintTower(image, p, c, tower, column, at.row, lowestFree, s, distance);
        } else if (visible) {
            // A face even from a hidden point: it rises into view above a nearer one.
            const double face =
                fromGround ? std::clamp((rise / run - kFaceSlope + kFaceBlend) / (2.0 * kFaceBlend),
                                        0.0, 1.0)
                           : 0.0;
            below = paintGround(ground, p, c, ray, at, before, face, painted, below, step,
                                lowestFree, image);
        }
        // On the row painted last (the fine steps) or below the image before any: seen.
        seen = (at.row <= lowestFree || lowestFree == p.height) && !onTower;
        painted = visible && !onTower;
        fromGround = !onTower;
        slope = rise / run;
        surfaceSlope = (at.surface - before.surface) / run;
        before = at;
        lowestFree = visible ? at.row : lowestFree;
        coarse = !fine;
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

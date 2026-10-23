#include "view_places.hpp"

#include "terrain/det_math.hpp"
#include "terrain/io/relief_view.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace view_places {

namespace {

constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr double kEyeAboveGround = 1.8;
// view_k2: the floor 8 to 14 km from the summit; the ground within kOwnTop of the summit is its
// own top.
constexpr std::initializer_list<double> kFloorDistances = {8'000.0, 10'000.0, 12'000.0,
                                                           14'000.0};
constexpr double kOwnTop = 300.0;
// view_wall: a lattice of kWallStep meters within kWallReach meters of the summit; the steepest
// rise over kWallRun steps; the wall is the rise within kWallSpan steps up from its foot.
constexpr double kWallStep = 20.0;
constexpr int kWallHalf = 200; // kWallReach / kWallStep
constexpr int kWallRun = 3;
constexpr int kWallSpan = 20;
constexpr float kApproachRise = 100.0f;

} // namespace

Point pointAt(const terrain::Heightfield& field, double x, double z) {
    return {x, z, field.heightAt(x, z)};
}

Point highestAround(const terrain::Heightfield& field, double x, double z, int half, double step) {
    Point best = pointAt(field, x, z);
    for (int j = -half; j <= half; ++j) {
        for (int i = -half; i <= half; ++i) {
            const Point p = pointAt(field, x + i * step, z + j * step);
            best = p.height > best.height ? p : best;
        }
    }
    return best;
}

Point summitNear(const terrain::Heightfield& field, double x, double z) {
    Point best = highestAround(field, x, z, 24, 500.0);
    for (const double step : {100.0, 25.0, 5.0}) {
        for (int k = 0; k < 64; ++k) {
            const Point next = highestAround(field, best.x, best.z, 4, step);
            if (!(next.height > best.height)) {
                break;
            }
            best = next;
        }
    }
    return best;
}

Point lowestAround(const terrain::Heightfield& field, double x, double z,
                   std::initializer_list<double> distances) {
    Point best{x, z + *distances.begin(), 1.0e9f};
    for (const double d : distances) {
        for (int k = 0; k < 64; ++k) {
            const Point p = pointAt(field, x + d * terrain::dmath::sinTurns(k / 64.0),
                                    z - d * terrain::dmath::cosTurns(k / 64.0));
            best = p.height >= 1.0f && p.height < best.height ? p : best;
        }
    }
    if (best.height == 1.0e9f) {
        best = pointAt(field, x, z + *distances.begin());
    }
    return best;
}

double headingTo(double x0, double z0, double x1, double z1) {
    return terrain::dmath::atan2(x1 - x0, -(z1 - z0)) / kTwoPi;
}

ValleyEye valleyEye(const terrain::Heightfield& field, const Point& summit) {
    const terrain::ViewGround ground = [&field](double x, double z) {
        return field.heightAt(x, z);
    };
    ValleyEye best;
    for (const double d : kFloorDistances) {
        for (int k = 0; k < 64; ++k) {
            const Point p = pointAt(field, summit.x + d * terrain::dmath::sinTurns(k / 64.0),
                                    summit.z - d * terrain::dmath::cosTurns(k / 64.0));
            if (p.height < 1.0f || (best.found && !(p.height < best.eye.height))) {
                continue;
            }
            terrain::ViewParams eye;
            eye.eyeX = p.x;
            eye.eyeZ = p.z;
            eye.eyeHeight = static_cast<double>(p.height) + kEyeAboveGround;
            eye.heading = headingTo(p.x, p.z, summit.x, summit.z);
            const terrain::Sightline line = terrain::sightline(
                ground, eye, summit.x, summit.z, static_cast<double>(summit.height), kOwnTop);
            if (line.visible) {
                best = {p, line.tangent, true};
            }
        }
    }
    return best;
}

Wall highestWall(const terrain::Heightfield& field, const Point& summit) {
    constexpr int n = 2 * kWallHalf + 1;
    std::vector<double> xs(static_cast<std::size_t>(n) * n);
    std::vector<double> zs(xs.size());
    std::vector<float> h(xs.size());
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const auto k = static_cast<std::size_t>(j) * n + static_cast<std::size_t>(i);
            xs[k] = summit.x + (i - kWallHalf) * kWallStep;
            zs[k] = summit.z + (j - kWallHalf) * kWallStep;
        }
    }
    field.heightsAt(xs, zs, h);
    const auto at = [&](int i, int j) {
        return h[static_cast<std::size_t>(j) * n + static_cast<std::size_t>(i)];
    };
    Wall best;
    int footI = kWallHalf;
    int footJ = kWallHalf;
    int di = 1;
    int dj = 0;
    for (int j = kWallSpan; j + kWallSpan < n; ++j) {
        for (int i = kWallSpan; i + kWallSpan < n; ++i) {
            for (const auto [u, v] :
                 {std::array<int, 2>{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1},
                  {-1, -1}}) {
                const double rise =
                    static_cast<double>(at(i + kWallRun * u, j + kWallRun * v) - at(i, j));
                if (rise > best.step &&
                    std::abs(at(i - kWallSpan * u, j - kWallSpan * v) - at(i, j)) <
                        kApproachRise) {
                    best.found = true;
                    best.step = rise;
                    footI = i;
                    footJ = j;
                    di = u;
                    dj = v;
                }
            }
        }
    }
    best.foot = {xs[static_cast<std::size_t>(footJ) * n + static_cast<std::size_t>(footI)],
                 zs[static_cast<std::size_t>(footJ) * n + static_cast<std::size_t>(footI)],
                 at(footI, footJ)};
    const auto back = static_cast<std::size_t>(footJ - kWallSpan * dj) * n +
                      static_cast<std::size_t>(footI - kWallSpan * di);
    best.approach = {xs[back], zs[back], h[back]};
    const double length = terrain::dmath::hypot(di, dj);
    best.upX = di / length;
    best.upZ = dj / length;
    float top = best.foot.height;
    for (int k = 1; k <= kWallSpan; ++k) {
        top = std::max(top, field.heightAt(best.foot.x + best.upX * kWallStep * k,
                                           best.foot.z + best.upZ * kWallStep * k));
    }
    best.height = static_cast<double>(top - best.foot.height);
    return best;
}

} // namespace view_places

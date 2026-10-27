// Where a tributary meets the river it joins (river_join.hpp).
#include "river_join.hpp"

#include "terrain/det_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace terrain::water {

namespace {

double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

void measure(std::vector<CoursePoint>& points) {
    points.front().s = 0.0;
    for (std::size_t i = 1; i < points.size(); ++i) {
        points[i].s = points[i - 1].s + dmath::hypot(points[i].x - points[i - 1].x,
                                                      points[i].z - points[i - 1].z);
    }
}

// A tributary meets the river it joins where its channel first comes within kJoinGap meters of
// the channel of that river: its course ends there. Where it first comes down into the valley
// that river carved more than kJoinCarve meters into the relief, it turns down to that channel
// (beyond, it would run along the other river at another level, on the walls of its valley). It
// ends at the water of that river.
constexpr double kJoinGap = 10.0;
constexpr double kJoinCarve = 5.0;
constexpr double kJoinStep = 16.0;

// The point of the axis of course c nearest to (x, z): its distance, where it lies, the water
// there and how far its channel and meanders reach from the axis.
struct Nearest {
    double distance = std::numeric_limits<double>::infinity();
    double x = 0.0;
    double z = 0.0;
    float level = 0.0f;
    double reach = 0.0;
};

Nearest nearestOn(const Course& c, double x, double z) {
    Nearest n;
    for (std::size_t k = 0; k + 1 < c.points.size(); ++k) {
        const CoursePoint& a = c.points[k];
        const CoursePoint& b = c.points[k + 1];
        const double dx = b.x - a.x;
        const double dz = b.z - a.z;
        const double length2 = dx * dx + dz * dz;
        const double t =
            length2 > 0.0 ? std::clamp(((x - a.x) * dx + (z - a.z) * dz) / length2, 0.0, 1.0)
                          : 0.0;
        const double d = dmath::hypot(x - a.x - t * dx, z - a.z - t * dz);
        if (d < n.distance) {
            n = {d, a.x + t * dx, a.z + t * dz, static_cast<float>(lerp(a.level, b.level, t)),
                 lerp(a.half + a.meander, b.half + b.meander, t)};
        }
    }
    return n;
}

// The course from point i down to the channel at n (in the images of the joined course, moved
// by j): a curve from kJoinRound meters before point i, toward it, then to the channel, sampled
// every kJoinStep meters (no corner).
constexpr double kJoinRound = 120.0;

void turnDown(std::vector<CoursePoint>& points, std::size_t i, const Nearest& n,
              const ReachInput::Join& j) {
    std::size_t start = i;
    while (start > 0 && points[start].s > points[i].s - kJoinRound) {
        --start;
    }
    const CoursePoint from = points[start];
    const CoursePoint bend = points[i];
    points.resize(start + 1);
    const double ux = bend.x + j.x - n.x;
    const double uz = bend.z + j.z - n.z;
    const double inside = std::max(0.0, n.reach - 2.0) / std::max(n.distance, 1.0);
    const double tx = n.x + inside * ux - j.x;
    const double tz = n.z + inside * uz - j.z;
    const double run = dmath::hypot(bend.x - from.x, bend.z - from.z) +
                       dmath::hypot(tx - bend.x, tz - bend.z);
    const auto steps = std::max(2, static_cast<int>(std::ceil(run / kJoinStep)));
    for (int k = 1; k <= steps; ++k) {
        const double u = static_cast<double>(k) / steps;
        CoursePoint p = bend;
        p.x = (1.0 - u) * (1.0 - u) * from.x + 2.0 * u * (1.0 - u) * bend.x + u * u * tx;
        p.z = (1.0 - u) * (1.0 - u) * from.z + 2.0 * u * (1.0 - u) * bend.z + u * u * tz;
        points.push_back(p);
    }
    measure(points);
}

} // namespace

float joinEarlier(const Heightfield& relief, const ReachInput& in,
                  std::vector<CoursePoint>& points) {
    if (in.joins[0].course == nullptr && in.joins[1].course == nullptr) {
        return in.b.level;
    }
    std::vector<double> xs(points.size());
    std::vector<double> zs(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        xs[i] = points[i].x;
        zs[i] = points[i].z;
    }
    std::vector<float> ground(points.size());
    relief.heightsAt(xs, zs, ground);
    const double length = std::max(points.back().s, 1.0);
    for (std::size_t i = 1; i + 1 < points.size(); ++i) {
        const double half = 0.5 * lerp(in.a.width, in.b.width, points[i].s / length);
        for (const ReachInput::Join& j : in.joins) {
            if (j.course == nullptr) {
                continue;
            }
            const Nearest n = nearestOn(*j.course, xs[i] + j.x, zs[i] + j.z);
            if (n.distance < n.reach + half + kJoinGap) {
                points.resize(i + 1);
                return std::min(in.a.level, n.level);
            }
            const double g = static_cast<double>(ground[i]);
            if (j.surface && j.surface(xs[i], zs[i], g) < g - kJoinCarve) {
                turnDown(points, i, n, j);
                return std::min(in.a.level, n.level);
            }
        }
    }
    return in.b.level;
}


} // namespace terrain::water

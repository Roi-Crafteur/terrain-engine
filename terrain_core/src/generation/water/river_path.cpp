// The lowest way of a reach across the relief and the smoothing of the ways into the axis of the
// course (river_course.hpp).
#include "river_course.hpp"

#include "terrain/det_math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace terrain::water {

namespace {

// The candidates of the sections, row by row: their positions and heights.
struct Ladder {
    int sections = 0; // the first and the last a single point
    int width = 1;    // candidates per section
    std::vector<double> xs;
    std::vector<double> zs;
    std::vector<float> heights;

    [[nodiscard]] std::size_t at(int k, int j) const {
        return static_cast<std::size_t>(k) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(j);
    }
};

Ladder ladderOf(const Heightfield& relief, double x0, double z0, double x1, double z1,
                const LadderParams& p) {
    const double dx = x1 - x0;
    const double dz = z1 - z0;
    const double length = dmath::hypot(dx, dz);
    Ladder l;
    l.sections = std::max(3, static_cast<int>(std::lround(length / p.sectionStep)) + 1);
    l.width = 2 * p.halfWidth + 1;
    const double ux = length > 0.0 ? dx / length : 1.0;
    const double uz = length > 0.0 ? dz / length : 0.0;
    const auto count = static_cast<std::size_t>(l.sections) * static_cast<std::size_t>(l.width);
    l.xs.resize(count);
    l.zs.resize(count);
    l.heights.assign(count, std::numeric_limits<float>::infinity());
    for (int k = 0; k < l.sections; ++k) {
        const double t = static_cast<double>(k) / (l.sections - 1);
        for (int j = 0; j < l.width; ++j) {
            const double aside = (j - p.halfWidth) * p.lateralStep;
            l.xs[l.at(k, j)] = x0 + t * dx - aside * uz;
            l.zs[l.at(k, j)] = z0 + t * dz + aside * ux;
        }
        // The candidates the way can reach from both ends, at most one aside per section.
        const int reach = std::min({p.halfWidth, k, l.sections - 1 - k});
        const auto first = l.at(k, p.halfWidth - reach);
        const auto span = static_cast<std::size_t>(2 * reach + 1);
        relief.heightsAt(std::span(l.xs).subspan(first, span),
                         std::span(l.zs).subspan(first, span),
                         std::span(l.heights).subspan(first, span));
    }
    return l;
}

// Binomial weights over kWindow sections on each side (a Gaussian of about 2 sections).
constexpr int kWindow = 8;
constexpr std::array<double, 2 * kWindow + 1> kWeights = {
    1, 16, 120, 560, 1820, 4368, 8008, 11440, 12870, 11440, 8008, 4368, 1820, 560, 120, 16, 1};
constexpr double kWeightSum = 65536.0;

// The chain of the ways: before (moved to end at the start of own, without its last point), own
// and after (moved to start at the end of own, without its first point).
struct Chain {
    std::vector<PathPoint> points;
    std::size_t first = 0; // index of the first point of own
};

Chain chainOf(const std::vector<PathPoint>* before, const std::vector<PathPoint>& own,
              const std::vector<PathPoint>* after) {
    Chain c;
    if (before != nullptr && before->size() > 1) {
        const double dx = own.front().x - before->back().x;
        const double dz = own.front().z - before->back().z;
        for (std::size_t i = 0; i + 1 < before->size(); ++i) {
            c.points.push_back({(*before)[i].x + dx, (*before)[i].z + dz});
        }
    }
    c.first = c.points.size();
    c.points.insert(c.points.end(), own.begin(), own.end());
    if (after != nullptr && after->size() > 1) {
        const double dx = own.back().x - after->front().x;
        const double dz = own.back().z - after->front().z;
        for (std::size_t i = 1; i < after->size(); ++i) {
            c.points.push_back({(*after)[i].x + dx, (*after)[i].z + dz});
        }
    }
    return c;
}

// The point k of the chain, beyond its ends by point reflection about them (the smoothing keeps
// the ends of the chain in place).
PathPoint pointOf(const Chain& c, std::ptrdiff_t k) {
    const auto n = static_cast<std::ptrdiff_t>(c.points.size());
    if (k < 0) {
        const PathPoint& end = c.points.front();
        const PathPoint& p = c.points[static_cast<std::size_t>(std::min(-k, n - 1))];
        return {2.0 * end.x - p.x, 2.0 * end.z - p.z};
    }
    if (k >= n) {
        const PathPoint& end = c.points.back();
        const PathPoint& p = c.points[static_cast<std::size_t>(std::max(2 * (n - 1) - k, std::ptrdiff_t{0}))];
        return {2.0 * end.x - p.x, 2.0 * end.z - p.z};
    }
    return c.points[static_cast<std::size_t>(k)];
}

PathPoint smoothAt(const Chain& c, std::size_t i) {
    double x = 0.0;
    double z = 0.0;
    const auto center = static_cast<std::ptrdiff_t>(i);
    for (int w = -kWindow; w <= kWindow; ++w) {
        const PathPoint p = pointOf(c, center + w);
        const double weight = kWeights[static_cast<std::size_t>(w + kWindow)];
        x += weight * (p.x - c.points[i].x);
        z += weight * (p.z - c.points[i].z);
    }
    return {c.points[i].x + x / kWeightSum, c.points[i].z + z / kWeightSum};
}

// The lowest of the highest ground over the ways of the ladder (minimax): the notch of the sill.
float notchOf(const Ladder& l, int center) {
    std::vector<float> best(l.heights.size(), std::numeric_limits<float>::infinity());
    best[l.at(0, center)] = l.heights[l.at(0, center)];
    for (int k = 1; k < l.sections; ++k) {
        for (int j = 0; j < l.width; ++j) {
            float lowest = std::numeric_limits<float>::infinity();
            for (int d = -1; d <= 1; ++d) {
                if (j + d >= 0 && j + d < l.width) {
                    lowest = std::min(lowest, best[l.at(k - 1, j + d)]);
                }
            }
            best[l.at(k, j)] = std::max(lowest, l.heights[l.at(k, j)]);
        }
    }
    return best[l.at(l.sections - 1, center)];
}

} // namespace

std::vector<PathPoint> lowestPath(const Heightfield& relief, double x0, double z0, double x1,
                                  double z1, const LadderParams& params) {
    Ladder l = ladderOf(relief, x0, z0, x1, z1, params);
    // The way crosses the sill at its notch: no candidate above it.
    const float notch = notchOf(l, params.halfWidth);
    for (float& h : l.heights) {
        if (h > notch) {
            h = std::numeric_limits<float>::infinity();
        }
    }
    const double bendCost = params.bend * params.lateralStep;
    std::vector<double> cost(l.heights.size(), std::numeric_limits<double>::infinity());
    std::vector<std::int8_t> from(l.heights.size(), 0);
    cost[l.at(0, params.halfWidth)] = static_cast<double>(l.heights[l.at(0, params.halfWidth)]);
    for (int k = 1; k < l.sections; ++k) {
        for (int j = 0; j < l.width; ++j) {
            const float h = l.heights[l.at(k, j)];
            if (!std::isfinite(h)) {
                continue;
            }
            double best = std::numeric_limits<double>::infinity();
            std::int8_t step = 0;
            for (int d = -1; d <= 1; ++d) {
                const int i = j + d;
                if (i < 0 || i >= l.width) {
                    continue;
                }
                const double c = cost[l.at(k - 1, i)] + (d != 0 ? bendCost : 0.0);
                if (c < best) {
                    best = c;
                    step = static_cast<std::int8_t>(d);
                }
            }
            cost[l.at(k, j)] = best + static_cast<double>(h);
            from[l.at(k, j)] = step;
        }
    }
    std::vector<PathPoint> path(static_cast<std::size_t>(l.sections));
    int j = params.halfWidth;
    for (int k = l.sections - 1; k >= 0; --k) {
        const std::size_t a = l.at(k, j);
        path[static_cast<std::size_t>(k)] = {l.xs[a], l.zs[a]};
        j += from[a];
    }
    // The ends exactly, whatever the rounding of the ladder.
    path.front() = {x0, z0};
    path.back() = {x1, z1};
    return path;
}

std::vector<PathPoint> smoothAxis(const std::vector<PathPoint>* before,
                                  const std::vector<PathPoint>& own,
                                  const std::vector<PathPoint>* after) {
    const Chain c = chainOf(before, own, after);
    std::vector<PathPoint> axis(own.size());
    for (std::size_t i = 0; i < own.size(); ++i) {
        axis[i] = smoothAt(c, c.first + i);
    }
    return axis;
}

PathPoint smoothedJoint(const std::vector<PathPoint>& upstream,
                        const std::vector<PathPoint>& downstream) {
    const Chain c = chainOf(&upstream, downstream, nullptr);
    return smoothAt(c, c.first);
}

} // namespace terrain::water

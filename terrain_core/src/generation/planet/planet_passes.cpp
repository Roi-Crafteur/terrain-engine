#include "terrain/generation/planet/planet_passes.hpp"

#include "planet_parallel.hpp"
#include "terrain/det_math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <utility>

namespace terrain::planet {

namespace {

constexpr int kLinkBlock = 256;
constexpr float kUnknown = std::numeric_limits<float>::quiet_NaN();

// The candidates of a ladder from a to b, row by row, and their heights (infinite where neither
// end can reach them, one aside per section), in buffers kept by each thread.
struct Ladder {
    int sections = 0;
    int width = 1;
    std::vector<double> xs;
    std::vector<double> zs;
    std::vector<float> heights;
    std::vector<float> best;

    [[nodiscard]] std::size_t at(int k, int j) const {
        return static_cast<std::size_t>(k) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(j);
    }
};

Ladder& ladderOf(const Heightfield& field, double ax, double az, double bx, double bz,
                 const PlanetPassParams& p, bool centerOnly) {
    thread_local Ladder l;
    const double dx = bx - ax;
    const double dz = bz - az;
    const double length = dmath::hypot(dx, dz);
    l.sections = std::max(3, static_cast<int>(std::lround(length / p.sectionStep)) + 1);
    l.width = 2 * p.halfWidth + 1;
    const double ux = length > 0.0 ? dx / length : 1.0;
    const double uz = length > 0.0 ? dz / length : 0.0;
    l.heights.assign(static_cast<std::size_t>(l.sections) * static_cast<std::size_t>(l.width),
                     std::numeric_limits<float>::infinity());
    for (int k = 0; k < l.sections; ++k) {
        const double t = static_cast<double>(k) / (l.sections - 1);
        const int reach = centerOnly ? 0 : std::min({p.halfWidth, k, l.sections - 1 - k});
        l.xs.clear();
        l.zs.clear();
        for (int j = p.halfWidth - reach; j <= p.halfWidth + reach; ++j) {
            const double aside = (j - p.halfWidth) * p.lateralStep;
            l.xs.push_back(ax + t * dx - aside * uz);
            l.zs.push_back(az + t * dz + aside * ux);
        }
        field.heightsAt(l.xs, l.zs,
                        std::span(l.heights).subspan(l.at(k, p.halfWidth - reach), l.xs.size()));
    }
    return l;
}

// The highest ground along the straight line, at the sections of the ladder.
float straightHeight(const Heightfield& field, double ax, double az, double bx, double bz,
                     const PlanetPassParams& p) {
    const Ladder& l = ladderOf(field, ax, az, bx, bz, p, true);
    float most = -std::numeric_limits<float>::infinity();
    for (int k = 0; k < l.sections; ++k) {
        most = std::max(most, l.heights[l.at(k, p.halfWidth)]);
    }
    return most;
}

std::uint64_t keyOf(const PlanetGrid& grid, const PlanetLink& link) {
    int direction = kNoNeighbor;
    for (int k = 0; k < 8; ++k) {
        if (grid.neighbor(link.cell, k) == link.next) {
            direction = k;
        }
    }
    return static_cast<std::uint64_t>(link.cell) * 9U + static_cast<std::uint64_t>(direction);
}

// The cut a link may make, under the mountains less the margin of what the ladder does not see.
float cutOf(const PlanetPassParams& p, std::uint8_t mountain) {
    return static_cast<float>(mountain != 0 ? p.mountainCut - p.margin : p.plainCut);
}

} // namespace

float crossingHeight(const Heightfield& field, double ax, double az, double bx, double bz,
                     const PlanetPassParams& params) {
    Ladder& l = ladderOf(field, ax, az, bx, bz, params, false);
    std::vector<float>& best = l.best;
    best.assign(l.heights.size(), std::numeric_limits<float>::infinity());
    best[l.at(0, params.halfWidth)] = l.heights[l.at(0, params.halfWidth)];
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
    return best[l.at(l.sections - 1, params.halfWidth)];
}

std::size_t raiseSills(const Heightfield& field, const PlanetGrid& grid,
                       const std::vector<PlanetLink>& links,
                       const std::vector<std::uint8_t>& mountain, const PlanetPassParams& params,
                       int threads, std::vector<float>& height,
                       std::vector<std::pair<std::uint64_t, float>>& crossings,
                       PlanetPasses& out) {
    std::vector<std::uint64_t> keys(links.size());
    std::vector<float> crossing(links.size(), kUnknown);
    for (std::size_t i = 0; i < links.size(); ++i) {
        keys[i] = keyOf(grid, links[i]);
        const auto it = std::lower_bound(crossings.begin(), crossings.end(),
                                         std::pair{keys[i], -std::numeric_limits<float>::infinity()});
        if (it != crossings.end() && it->first == keys[i]) {
            crossing[i] = it->second;
        }
    }
    std::vector<std::uint8_t> fresh(links.size(), 0);
    parallelBlocks(static_cast<int>(links.size()), kLinkBlock, threads, [&](int begin, int end) {
        for (int i = begin; i < end; ++i) {
            const auto n = static_cast<std::size_t>(i);
            if (!std::isnan(crossing[n])) {
                continue;
            }
            const PlanetLink& l = links[n];
            const float above = height[l.cell] + cutOf(params, mountain[l.cell]);
            // The straight line first: the ladder is needed only if it rises above the cut.
            const float straight = straightHeight(field, l.ax, l.az, l.bx, l.bz, params);
            crossing[n] = straight > above ? crossingHeight(field, l.ax, l.az, l.bx, l.bz, params)
                                           : straight;
            fresh[n] = 1;
        }
    });
    std::size_t raised = 0;
    for (std::size_t i = 0; i < links.size(); ++i) {
        if (fresh[i] != 0) {
            crossings.emplace_back(keys[i], crossing[i]);
            ++out.checked;
        }
        const PlanetLink& l = links[i];
        const float need = crossing[i] - cutOf(params, mountain[l.cell]);
        if (need > height[l.cell]) {
            out.highestRaise = std::max(out.highestRaise, need - height[l.cell]);
            height[l.cell] = need;
            ++raised;
        }
    }
    std::sort(crossings.begin(), crossings.end());
    out.raised += raised;
    return raised;
}

} // namespace terrain::planet

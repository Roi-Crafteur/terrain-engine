#include "terrain/generation/planet/planet_passes.hpp"

#include "planet_parallel.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/planet/planet_relief.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <span>
#include <utility>

namespace terrain::planet {

namespace {

constexpr std::size_t kNoSlot = static_cast<std::size_t>(-1);
// The direction opposite to each of kPlanetNeighbors.
constexpr std::array<int, 8> kOpposite = {1, 0, 3, 2, 7, 6, 5, 4};
// Each edge is measured once, from the cell it leaves toward these directions (east, south and
// the two diagonals south), or from the only measured cell of the two.
constexpr std::array<bool, 8> kForward = {true, false, true, false, true, true, false, false};

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

// One edge measured: from cell toward its neighbor k, at level.
struct Measured {
    std::size_t cell = 0;
    int k = 0;
    std::int16_t level = PlanetCols::kNone;
    bool ladder = false;
};

std::int16_t wholeMeters(float level) {
    const double up = std::ceil(static_cast<double>(level));
    return static_cast<std::int16_t>(std::clamp(up, -32000.0, 32000.0));
}

} // namespace

PlanetCols::PlanetCols(const PlanetGrid& grid, const std::vector<std::uint8_t>& measured)
    : grid_(grid) {
    words_.assign((grid.cells() + 63) / 64, 0);
    for (std::size_t c = 0; c < grid.cells(); ++c) {
        if (measured[c] != 0) {
            words_[c >> 6] |= std::uint64_t{1} << (c & 63);
        }
    }
    rank_.resize(words_.size());
    std::uint32_t total = 0;
    for (std::size_t w = 0; w < words_.size(); ++w) {
        rank_[w] = total;
        total += static_cast<std::uint32_t>(std::popcount(words_[w]));
    }
    levels_.assign(static_cast<std::size_t>(total) * 8, kNone);
}

std::size_t PlanetCols::slot(std::size_t cell) const {
    if (words_.empty() || ((words_[cell >> 6] >> (cell & 63)) & 1U) == 0) {
        return kNoSlot;
    }
    const std::uint64_t below = (std::uint64_t{1} << (cell & 63)) - 1;
    return rank_[cell >> 6] + static_cast<std::size_t>(std::popcount(words_[cell >> 6] & below));
}

float PlanetCols::level(std::size_t cell, int k) const {
    std::size_t s = slot(cell);
    int side = k;
    if (s == kNoSlot) {
        s = slot(grid_.neighbor(cell, k));
        side = kOpposite[static_cast<std::size_t>(k)];
    }
    const std::int16_t l =
        s == kNoSlot ? kNone : levels_[s * 8 + static_cast<std::size_t>(side)];
    return l == kNone ? -std::numeric_limits<float>::infinity() : static_cast<float>(l);
}

bool PlanetCols::held(const std::vector<float>& height, std::size_t cell, int k,
                      float* at) const {
    if (levels_.empty()) {
        return false;
    }
    const float l = level(cell, k);
    const bool held = l > std::max(height[cell], height[grid_.neighbor(cell, k)]);
    if (held && at != nullptr) {
        *at = l;
    }
    return held;
}

void PlanetCols::set(std::size_t cell, int k, std::int16_t level) {
    const std::size_t s = slot(cell);
    if (s != kNoSlot) {
        levels_[s * 8 + static_cast<std::size_t>(k)] = level;
    }
    const std::size_t n = grid_.neighbor(cell, k);
    const std::size_t t = slot(n);
    if (t != kNoSlot) {
        levels_[t * 8 + static_cast<std::size_t>(kOpposite[static_cast<std::size_t>(k)])] = level;
    }
}

std::size_t PlanetCols::bytes() const {
    return words_.size() * sizeof(std::uint64_t) + rank_.size() * sizeof(std::uint32_t) +
           levels_.size() * sizeof(std::int16_t);
}

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

PlanetCols measureCols(const Heightfield& field, const PlanetGrid& grid,
                       const std::vector<float>& height, const std::vector<std::uint8_t>& floor,
                       int floorSamples, const std::vector<std::uint8_t>& ocean,
                       const std::vector<std::uint8_t>& mountain, const PlanetPassParams& params,
                       int threads, PlanetPasses& out) {
    std::vector<std::uint8_t> measured(grid.cells(), 0);
    for (std::size_t c = 0; c < grid.cells(); ++c) {
        measured[c] = ocean[c] == kLand && (!params.measured || params.measured(c)) ? 1 : 0;
    }
    PlanetCols cols(grid, measured);
    const int blocks = (grid.nz + kRowBlock - 1) / kRowBlock;
    std::vector<std::vector<Measured>> found(static_cast<std::size_t>(blocks));
    parallelRows(grid.nz, threads, [&](int first, int last) {
        std::vector<Measured>& mine = found[static_cast<std::size_t>(first / kRowBlock)];
        for (std::size_t c = grid.index(0, first); c < grid.index(0, last); ++c) {
            if (measured[c] == 0) {
                continue;
            }
            const std::array<double, 2> a = floorPoint(grid, c, floor[c], floorSamples);
            for (int k = 0; k < 8; ++k) {
                const std::size_t n = grid.neighbor(c, k);
                if (ocean[n] != kLand ||
                    !(kForward[static_cast<std::size_t>(k)] || measured[n] == 0)) {
                    continue;
                }
                const std::array<double, 2> p = floorPoint(grid, n, floor[n], floorSamples);
                const double bx = a[0] + wrapCoordinate(p[0] - a[0], grid.periodX());
                const double bz = a[1] + wrapCoordinate(p[1] - a[1], grid.periodZ());
                const float cut = static_cast<float>(mountain[c] != 0 || mountain[n] != 0
                                                         ? params.mountainCut - params.margin
                                                         : params.plainCut);
                const float straight = straightHeight(field, a[0], a[1], bx, bz, params);
                const bool ladder = straight - cut > std::min(height[c], height[n]);
                const float col =
                    ladder ? crossingHeight(field, a[0], a[1], bx, bz, params) : straight;
                mine.push_back({c, k, wholeMeters(col - cut), ladder});
            }
        }
    });
    for (const std::vector<Measured>& block : found) {
        for (const Measured& m : block) {
            cols.set(m.cell, m.k, m.level);
            const float higher = std::max(height[m.cell], height[grid.neighbor(m.cell, m.k)]);
            ++out.edges;
            out.ladders += m.ladder ? 1U : 0U;
            if (static_cast<float>(m.level) > higher) {
                ++out.held;
                out.highest = std::max(out.highest, static_cast<float>(m.level) - higher);
            }
        }
    }
    return cols;
}

} // namespace terrain::planet

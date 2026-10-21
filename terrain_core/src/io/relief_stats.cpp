#include "terrain/io/relief_stats.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <queue>
#include <stdexcept>
#include <utility>

namespace terrain {

namespace {

using Index = std::size_t;

// Cells of the wrapping grid and their 8 neighbors: the 4 sides first, then the diagonals.
struct Torus {
    int columns;
    int rows;

    [[nodiscard]] Index cells() const {
        return static_cast<Index>(columns) * static_cast<Index>(rows);
    }
    [[nodiscard]] Index index(int i, int j) const {
        const int wi = (i % columns + columns) % columns;
        const int wj = (j % rows + rows) % rows;
        return static_cast<Index>(wj) * static_cast<Index>(columns) + static_cast<Index>(wi);
    }
    [[nodiscard]] int column(Index cell) const {
        return static_cast<int>(cell % static_cast<Index>(columns));
    }
    [[nodiscard]] int row(Index cell) const {
        return static_cast<int>(cell / static_cast<Index>(columns));
    }
    [[nodiscard]] Index neighbor(Index cell, int k) const {
        static constexpr std::array<int, 8> kDx = {1, -1, 0, 0, 1, -1, 1, -1};
        static constexpr std::array<int, 8> kDz = {0, 0, 1, -1, 1, 1, -1, -1};
        return index(column(cell) + kDx[static_cast<std::size_t>(k)],
                     row(cell) + kDz[static_cast<std::size_t>(k)]);
    }
};

constexpr double kDiagonal = 1.4142135623730951;

// 8-connected components of the cells where member is true: the component of each cell (-1
// outside) and the cells of each component, in the order of their first cell.
struct Components {
    std::vector<std::int32_t> of;
    std::vector<std::vector<Index>> cells;
};

Components components(const Torus& torus, const std::vector<bool>& member) {
    Components result;
    result.of.assign(torus.cells(), -1);
    for (Index start = 0; start < torus.cells(); ++start) {
        if (!member[start] || result.of[start] >= 0) {
            continue;
        }
        const auto id = static_cast<std::int32_t>(result.cells.size());
        std::vector<Index> queue{start};
        result.of[start] = id;
        for (std::size_t head = 0; head < queue.size(); ++head) {
            for (int k = 0; k < 8; ++k) {
                const Index n = torus.neighbor(queue[head], k);
                if (member[n] && result.of[n] < 0) {
                    result.of[n] = id;
                    queue.push_back(n);
                }
            }
        }
        result.cells.push_back(std::move(queue));
    }
    return result;
}

// Running extremum (max if takeMax) over 2 radius + 1 cells along rows, then along columns.
std::vector<float> squareFilter(const Torus& torus, const std::vector<float>& values, int radius,
                                bool takeMax) {
    const auto pick = [takeMax](float a, float b) {
        return takeMax ? std::max(a, b) : std::min(a, b);
    };
    std::vector<float> along(values.size());
    for (int j = 0; j < torus.rows; ++j) {
        for (int i = 0; i < torus.columns; ++i) {
            float v = values[torus.index(i, j)];
            for (int d = 1; d <= radius; ++d) {
                v = pick(v, pick(values[torus.index(i - d, j)], values[torus.index(i + d, j)]));
            }
            along[torus.index(i, j)] = v;
        }
    }
    std::vector<float> out(values.size());
    for (int j = 0; j < torus.rows; ++j) {
        for (int i = 0; i < torus.columns; ++i) {
            float v = along[torus.index(i, j)];
            for (int d = 1; d <= radius; ++d) {
                v = pick(v, pick(along[torus.index(i, j - d)], along[torus.index(i, j + d)]));
            }
            out[torus.index(i, j)] = v;
        }
    }
    return out;
}

std::vector<float> asValues(const std::vector<bool>& mask) {
    std::vector<float> values(mask.size());
    for (std::size_t k = 0; k < mask.size(); ++k) {
        values[k] = mask[k] ? 1.0f : 0.0f;
    }
    return values;
}

// The mask dilated then eroded by a square of 2 radius + 1 cells.
std::vector<bool> closeMask(const Torus& torus, const std::vector<bool>& mask, int radius) {
    const std::vector<float> dilated = squareFilter(torus, asValues(mask), radius, true);
    const std::vector<float> closed = squareFilter(torus, dilated, radius, false);
    std::vector<bool> out(mask.size());
    for (std::size_t k = 0; k < mask.size(); ++k) {
        out[k] = closed[k] > 0.5f;
    }
    return out;
}

// Shortest paths (steps 1 and sqrt 2) inside a component from one of its cells.
void pathsFrom(const Torus& torus, const Components& parts, std::int32_t id, Index source,
               std::vector<double>& distance) {
    using Item = std::pair<double, Index>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> queue;
    for (const Index cell : parts.cells[static_cast<std::size_t>(id)]) {
        distance[cell] = std::numeric_limits<double>::infinity();
    }
    distance[source] = 0.0;
    queue.emplace(0.0, source);
    while (!queue.empty()) {
        const auto [d, cell] = queue.top();
        queue.pop();
        if (d > distance[cell]) {
            continue;
        }
        for (int k = 0; k < 8; ++k) {
            const Index n = torus.neighbor(cell, k);
            const double next = d + (k < 4 ? 1.0 : kDiagonal);
            if (parts.of[n] == id && next < distance[n]) {
                distance[n] = next;
                queue.emplace(next, n);
            }
        }
    }
}

// The farthest cell of the component from source, and its distance in cells.
std::pair<Index, double> farthest(const Torus& torus, const Components& parts, std::int32_t id,
                                  Index source, std::vector<double>& distance) {
    pathsFrom(torus, parts, id, source, distance);
    std::pair<Index, double> best{source, 0.0};
    for (const Index cell : parts.cells[static_cast<std::size_t>(id)]) {
        if (distance[cell] > best.second) {
            best = {cell, distance[cell]};
        }
    }
    return best;
}

std::vector<MountainChain> measureChains(const ReliefGrid& grid, const Torus& torus,
                                         const std::vector<bool>& chain) {
    const Components parts = components(torus, chain);
    std::vector<double> distance(torus.cells(), 0.0);
    std::vector<MountainChain> chains;
    const double area = grid.spacing * grid.spacing;
    for (std::size_t id = 0; id < parts.cells.size(); ++id) {
        const std::vector<Index>& cells = parts.cells[id];
        const auto component = static_cast<std::int32_t>(id);
        // Double sweep: the farthest cell from any cell, then the farthest from that one.
        const Index end = farthest(torus, parts, component, cells.front(), distance).first;
        const double steps = farthest(torus, parts, component, end, distance).second;
        MountainChain c;
        c.length = (steps + 1.0) * grid.spacing;
        c.area = static_cast<double>(cells.size()) * area;
        c.width = c.area / c.length;
        Index top = cells.front();
        for (const Index cell : cells) {
            top = grid.heights[cell] > grid.heights[top] ? cell : top;
        }
        c.highest = grid.heights[top];
        c.x = grid.x(torus.column(top));
        c.z = grid.z(torus.row(top));
        chains.push_back(c);
    }
    std::stable_sort(chains.begin(), chains.end(),
                     [](const MountainChain& a, const MountainChain& b) {
                         return a.length > b.length;
                     });
    return chains;
}

// Approximate distance (chamfer 1, sqrt 2, in cells) to the nearest source cell, on the torus:
// forward and backward sweeps, twice so that distances cross the edges.
std::vector<float> chamferDistance(const Torus& torus, const std::vector<bool>& source) {
    const float far = std::numeric_limits<float>::max() / 4.0f;
    std::vector<float> d(torus.cells());
    for (std::size_t k = 0; k < d.size(); ++k) {
        d[k] = source[k] ? 0.0f : far;
    }
    const auto relax = [&](int i, int j, int di, int dj, float step) {
        float& here = d[torus.index(i, j)];
        here = std::min(here, d[torus.index(i + di, j + dj)] + step);
    };
    const auto diagonal = static_cast<float>(kDiagonal);
    for (int pass = 0; pass < 2; ++pass) {
        for (int j = 0; j < torus.rows; ++j) {
            for (int i = 0; i < torus.columns; ++i) {
                relax(i, j, -1, 0, 1.0f);
                relax(i, j, 0, -1, 1.0f);
                relax(i, j, -1, -1, diagonal);
                relax(i, j, 1, -1, diagonal);
            }
        }
        for (int j = torus.rows - 1; j >= 0; --j) {
            for (int i = torus.columns - 1; i >= 0; --i) {
                relax(i, j, 1, 0, 1.0f);
                relax(i, j, 0, 1, 1.0f);
                relax(i, j, 1, 1, diagonal);
                relax(i, j, -1, 1, diagonal);
            }
        }
    }
    return d;
}

// The land figures; returns the cells of the land masses of at least 1,000,000 km2.
std::vector<bool> measureLand(const ReliefGrid& grid, const Torus& torus,
                              const ReliefStatsParams& params, ReliefStats& stats) {
    std::vector<bool> land(torus.cells());
    std::size_t landCells = 0;
    std::array<std::size_t, kAltitudeBands.size() + 1> bands{};
    Index top = 0;
    for (Index cell = 0; cell < torus.cells(); ++cell) {
        const float h = grid.heights[cell];
        top = h > grid.heights[top] ? cell : top;
        land[cell] = h >= kSeaLevel;
        if (!land[cell]) {
            continue;
        }
        ++landCells;
        const auto band = std::upper_bound(kAltitudeBands.begin(), kAltitudeBands.end(), h);
        ++bands[static_cast<std::size_t>(band - kAltitudeBands.begin())];
    }
    stats.landShare = static_cast<double>(landCells) / static_cast<double>(torus.cells());
    for (std::size_t b = 0; b < bands.size(); ++b) {
        stats.altitudeShares[b] =
            landCells > 0 ? static_cast<double>(bands[b]) / static_cast<double>(landCells) : 0.0;
    }
    stats.highest = grid.heights[top];
    stats.highestX = grid.x(torus.column(top));
    stats.highestZ = grid.z(torus.row(top));
    const Components masses = components(torus, land);
    std::vector<bool> large(torus.cells(), false);
    for (const std::vector<Index>& cells : masses.cells) {
        const double area = static_cast<double>(cells.size()) * grid.spacing * grid.spacing;
        stats.landMasses.push_back({area, grid.x(torus.column(cells.front())),
                                    grid.z(torus.row(cells.front()))});
        stats.continents += area >= params.continentArea ? 1 : 0;
        for (const Index cell : cells) {
            large[cell] = area >= 1.0e12;
        }
    }
    std::stable_sort(stats.landMasses.begin(), stats.landMasses.end(),
                     [](const LandMass& a, const LandMass& b) { return a.area > b.area; });
    return large;
}

} // namespace

namespace {

// Cells radius cells around (i, j) that are land, as a share.
double landAround(const ReliefGrid& grid, const Torus& torus, Index cell, int radius) {
    int land = 0;
    const int i = torus.column(cell);
    const int j = torus.row(cell);
    for (int b = -radius; b <= radius; ++b) {
        for (int a = -radius; a <= radius; ++a) {
            land += grid.heights[torus.index(i + a, j + b)] >= kSeaLevel ? 1 : 0;
        }
    }
    const int side = 2 * radius + 1;
    return static_cast<double>(land) / (side * side);
}

ReliefSite siteOf(const ReliefGrid& grid, const Torus& torus, Index cell) {
    return {true, grid.x(torus.column(cell)), grid.z(torus.row(cell))};
}

// The coast with as much land as sea within 100 km, among the coast cells within 600 km of a
// chain if any (else among all), every few cells.
ReliefSite pickCoast(const ReliefGrid& grid, const Torus& torus, const ReliefStats& stats,
                     const std::vector<float>& toChain) {
    const int radius = std::max(1, static_cast<int>(std::lround(100'000.0 / grid.spacing)));
    const double nearChain = 600'000.0 / grid.spacing;
    for (const bool requireChain : {true, false}) {
        std::vector<Index> coast;
        for (Index cell = 0; cell < torus.cells(); ++cell) {
            if ((stats.cells[cell] & kReliefCoast) != 0 &&
                (!requireChain || toChain[cell] <= nearChain)) {
                coast.push_back(cell);
            }
        }
        if (coast.empty()) {
            continue;
        }
        const std::size_t stride = std::max<std::size_t>(1, coast.size() / 4000);
        Index best = coast.front();
        double bestGap = 2.0;
        for (std::size_t k = 0; k < coast.size(); k += stride) {
            const double gap = std::abs(landAround(grid, torus, coast[k], radius) - 0.5);
            if (gap < bestGap) {
                best = coast[k];
                bestGap = gap;
            }
        }
        return siteOf(grid, torus, best);
    }
    return {};
}

// The share of land within radius cells (a square of 2 radius + 1), minus one half: its coast
// line is the coast at a coarser scale.
ReliefGrid smoothedLand(const ReliefGrid& grid, const Torus& torus, int radius) {
    std::vector<float> land(torus.cells());
    for (Index cell = 0; cell < torus.cells(); ++cell) {
        land[cell] = grid.heights[cell] >= kSeaLevel ? 1.0f : 0.0f;
    }
    ReliefGrid out = grid;
    std::vector<float> along(land.size());
    for (int pass = 0; pass < 2; ++pass) {
        const std::vector<float>& in = pass == 0 ? land : along;
        std::vector<float>& to = pass == 0 ? along : out.heights;
        for (int j = 0; j < torus.rows; ++j) {
            for (int i = 0; i < torus.columns; ++i) {
                float sum = 0.0f;
                for (int d = -radius; d <= radius; ++d) {
                    sum += pass == 0 ? in[torus.index(i + d, j)] : in[torus.index(i, j + d)];
                }
                to[torus.index(i, j)] = sum / static_cast<float>(2 * radius + 1);
            }
        }
    }
    for (float& h : out.heights) {
        h -= 0.5f;
    }
    return out;
}

// Mountain cells: high land whose heights around span enough.
std::vector<bool> mountainCells(const ReliefGrid& grid, const Torus& torus,
                                const ReliefStatsParams& params) {
    const int radius = std::max(1, static_cast<int>(std::lround(params.reliefRadius / grid.spacing)));
    const std::vector<float> highest = squareFilter(torus, grid.heights, radius, true);
    const std::vector<float> lowest = squareFilter(torus, grid.heights, radius, false);
    std::vector<bool> mountain(torus.cells());
    for (Index cell = 0; cell < torus.cells(); ++cell) {
        mountain[cell] = grid.heights[cell] >= params.mountainHeight &&
                         highest[cell] - lowest[cell] >= params.mountainRelief;
    }
    return mountain;
}

// The bits of every cell, the shares of the land they cover and the land of the large land
// masses farthest from the chains.
void classify(const ReliefGrid& grid, const Torus& torus, const ReliefStatsParams& params,
              const std::vector<bool>& mountain, const std::vector<bool>& chain,
              const std::vector<float>& toChain, const std::vector<bool>& large,
              ReliefStats& stats) {
    stats.cells.assign(torus.cells(), 0);
    std::array<std::size_t, 5> counts{}; // land, mountain, chain, plain, high plain
    const double plainCells = params.plainDistance / grid.spacing;
    Index farthestLand = 0;
    bool anyLand = false;
    for (Index cell = 0; cell < torus.cells(); ++cell) {
        if (grid.heights[cell] < kSeaLevel) {
            continue;
        }
        unsigned bits = kReliefLand;
        bits |= mountain[cell] ? kReliefMountain : 0u;
        bits |= chain[cell] ? kReliefChain : 0u;
        if (toChain[cell] >= plainCells) {
            bits |= kReliefPlain;
            counts[4] += grid.heights[cell] > 1500.0f ? 1 : 0;
        }
        if (large[cell] && (!anyLand || toChain[cell] > toChain[farthestLand])) {
            farthestLand = cell;
            anyLand = true;
        }
        for (int k = 0; k < 4; ++k) {
            bits |= grid.heights[torus.neighbor(cell, k)] < kSeaLevel ? kReliefCoast : 0u;
        }
        stats.cells[cell] = static_cast<std::uint8_t>(bits);
        ++counts[0];
        counts[1] += mountain[cell] ? 1 : 0;
        counts[2] += chain[cell] ? 1 : 0;
        counts[3] += (bits & kReliefPlain) != 0u ? 1 : 0;
    }
    const auto share = [](std::size_t part, std::size_t whole) {
        return whole > 0 ? static_cast<double>(part) / static_cast<double>(whole) : 0.0;
    };
    stats.mountainShare = share(counts[1], counts[0]);
    stats.chainShare = share(counts[2], counts[0]);
    stats.plainShare = share(counts[3], counts[0]);
    stats.highPlainShare = share(counts[4], counts[3]);
    if (anyLand) {
        stats.plain = siteOf(grid, torus, farthestLand);
        stats.plainSiteDistance = static_cast<double>(toChain[farthestLand]) * grid.spacing;
    }
}

} // namespace

ReliefStats measureRelief(const ReliefGrid& world, const ReliefStatsParams& params) {
    if (!world.wraps || world.columns < 3 || world.rows < 3) {
        throw std::invalid_argument("measureRelief: the grid must cover the whole world");
    }
    const Torus torus{world.columns, world.rows};
    ReliefStats stats;
    const std::vector<bool> large = measureLand(world, torus, params, stats);
    const std::vector<bool> mountain = mountainCells(world, torus, params);
    const int bridge = std::max(1, static_cast<int>(std::lround(params.bridge / world.spacing)));
    std::vector<bool> chain = closeMask(torus, mountain, bridge);
    for (Index cell = 0; cell < torus.cells(); ++cell) {
        chain[cell] = chain[cell] && world.heights[cell] >= kSeaLevel;
    }
    stats.chains = measureChains(world, torus, chain);
    const std::vector<float> toChain = chamferDistance(torus, chain);
    classify(world, torus, params, mountain, chain, toChain, large, stats);
    const std::vector<relief_shapes::Line> lines = relief_shapes::coastLines(world);
    for (const relief_shapes::Line& line : lines) {
        stats.coastLength += relief_shapes::lineLength(line);
    }
    stats.longestStraightCoast =
        relief_shapes::longestStraight(lines, params.straightTolerance, params.straightRelative);
    const int smoothing =
        std::max(1, static_cast<int>(std::lround(params.coastSmoothing / world.spacing)));
    stats.longestStraightMapCoast = relief_shapes::longestStraight(
        relief_shapes::coastLines(smoothedLand(world, torus, smoothing)),
        params.straightTolerance, params.straightRelative);
    if (!stats.chains.empty()) {
        stats.mountain = {true, stats.chains.front().x, stats.chains.front().z};
    }
    stats.coast = pickCoast(world, torus, stats, toChain);
    return stats;
}

} // namespace terrain

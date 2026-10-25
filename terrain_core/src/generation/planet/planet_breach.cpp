#include "terrain/generation/planet/planet_breach.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_relief.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <queue>
#include <utility>

namespace terrain::planet {

namespace {

// Each cell of a carved way lies this much below the one before it: strictly downhill.
constexpr float kChannelStep = 0.001f;
constexpr std::uint32_t kNone = 0xFFFFFFFFu;
// The direction opposite to each of kPlanetNeighbors.
constexpr std::array<std::uint8_t, 8> kOpposite = {1, 0, 3, 2, 7, 6, 5, 4};

// The Priority-Flood: the fill level of every cell (the sea level on the ocean), the direction
// toward the cell it was flooded from (kNoNeighbor on the ocean) and the land cells in flood order.
struct Flood {
    std::vector<float> level;
    std::vector<std::uint8_t> parent;
    std::vector<std::uint32_t> order;
};

struct Front {
    float level;
    std::uint32_t cell;
    bool operator>(const Front& other) const {
        return level > other.level || (level == other.level && cell > other.cell);
    }
};

Flood priorityFlood(const PlanetGrid& grid, const std::vector<float>& height,
                    const std::vector<std::uint8_t>& ocean, const PlanetCols* cols) {
    Flood f;
    f.level.assign(grid.cells(), kSeaLevel);
    f.parent.assign(grid.cells(), kNoNeighbor);
    std::vector<std::uint8_t> seen(grid.cells(), 0);
    std::priority_queue<Front, std::vector<Front>, std::greater<>> front;
    for (std::size_t c = 0; c < grid.cells(); ++c) {
        seen[c] = ocean[c] != kLand ? 1 : 0;
    }
    // The ocean along the coasts: the rest of it is never reached from the land.
    for (std::size_t c = 0; c < grid.cells(); ++c) {
        bool coast = false;
        for (int k = 0; k < 8 && ocean[c] != kLand; ++k) {
            coast = coast || ocean[grid.neighbor(c, k)] == kLand;
        }
        if (coast) {
            front.push({kSeaLevel, static_cast<std::uint32_t>(c)});
        }
    }
    while (!front.empty()) {
        const Front top = front.top();
        front.pop();
        if (ocean[top.cell] == kLand) {
            f.order.push_back(top.cell);
        }
        for (int k = 0; k < 8; ++k) {
            const std::size_t n = grid.neighbor(top.cell, k);
            if (seen[n] != 0) {
                continue;
            }
            seen[n] = 1;
            f.parent[n] = kOpposite[static_cast<std::size_t>(k)];
            f.level[n] = std::max(height[n], top.level);
            float col = 0.0f;
            if (cols != nullptr && ocean[top.cell] == kLand && ocean[n] == kLand &&
                cols->held(height, top.cell, k, &col)) {
                f.level[n] = std::max(f.level[n], col); // the water passes over the col
            }
            front.push({f.level[n], static_cast<std::uint32_t>(n)});
        }
    }
    return f;
}

// A closed depression: its outlet, its lowest height, its cells and whether one of them or the
// outlet lies under the mountains.
struct Depression {
    std::uint32_t outlet = kNone;
    float lowest = 0.0f;
    float fill = 0.0f; // the highest fill level of its cells: above its outlet behind a col
    std::size_t cells = 0;
    bool mountain = false;
    std::vector<std::uint32_t> pits;
};

bool isPit(const PlanetGrid& grid, const std::vector<float>& height, std::size_t c) {
    for (int k = 0; k < 8; ++k) {
        if (lowerCell(height, grid.neighbor(c, k), c)) {
            return false;
        }
    }
    return true;
}

// The depressions, by outlet: the cells below their fill level, each attached to the first cell
// at its fill level on its way to the ocean.
std::vector<Depression> depressionsOf(const PlanetGrid& grid, const std::vector<float>& height,
                                      const std::vector<std::uint8_t>& mountain, const Flood& f) {
    std::vector<std::uint32_t> outlet(grid.cells(), kNone);
    std::vector<std::pair<std::uint32_t, std::uint32_t>> members; // outlet, cell
    for (const std::uint32_t c : f.order) {
        if (!(f.level[c] > height[c])) {
            continue;
        }
        const std::size_t up = grid.neighbor(c, f.parent[c]);
        outlet[c] = outlet[up] != kNone ? outlet[up] : static_cast<std::uint32_t>(up);
        members.emplace_back(outlet[c], c);
    }
    std::sort(members.begin(), members.end());
    std::vector<Depression> out;
    for (const auto& [exit, c] : members) {
        if (out.empty() || out.back().outlet != exit) {
            out.push_back({exit, height[c], f.level[c], 0, mountain[exit] != 0, {}});
        }
        Depression& d = out.back();
        d.lowest = std::min(d.lowest, height[c]);
        d.fill = std::max(d.fill, f.level[c]);
        ++d.cells;
        d.mountain = d.mountain || mountain[c] != 0;
        if (isPit(grid, height, c)) {
            d.pits.push_back(c);
        }
    }
    return out;
}

// The drained area of the outlets, m^2: the cells upstream of each in the flood tree.
std::vector<std::uint32_t> drainedCells(const PlanetGrid& grid, const Flood& f) {
    std::vector<std::uint32_t> count(grid.cells(), 1);
    for (auto it = f.order.rbegin(); it != f.order.rend(); ++it) {
        const std::size_t up = grid.neighbor(*it, f.parent[*it]);
        count[up] += count[*it];
    }
    return count;
}

double allowedDepth(const PlanetBreachParams& p, double area, bool mountain) {
    const double t = std::clamp(dmath::log(std::max(area, 1.0) / p.areaLow) /
                                    dmath::log(p.areaHigh / p.areaLow),
                                0.0, 1.0);
    const double depth = p.depthLow + (p.depthHigh - p.depthLow) * t;
    return mountain ? std::min(depth, p.mountainDepth) : depth;
}

// Lowers the way from pit toward the ocean below the pit, until a cell lower still (never below
// the sea level).
void carveFrom(const PlanetGrid& grid, std::vector<float>& height,
               const std::vector<std::uint8_t>& ocean, const Flood& f, std::uint32_t pit,
               PlanetBreach& out) {
    float target = height[pit];
    std::size_t c = grid.neighbor(pit, f.parent[pit]);
    while (ocean[c] == kLand) {
        target -= kChannelStep;
        if (!(height[c] > target) || target < kSeaLevel) {
            break;
        }
        out.deepestCut = std::max(out.deepestCut, height[c] - target);
        ++out.cutCells;
        height[c] = target;
        c = grid.neighbor(c, f.parent[c]);
    }
}

} // namespace

PlanetBreach breachDepressions(const PlanetGrid& grid, std::vector<float>& height,
                               const std::vector<std::uint8_t>& ocean,
                               const std::vector<std::uint8_t>& mountain,
                               const PlanetBreachParams& params, const PlanetCols* cols) {
    PlanetBreach out;
    const Flood f = priorityFlood(grid, height, ocean, cols);
    std::vector<Depression> depressions = depressionsOf(grid, height, mountain, f);
    const std::vector<std::uint32_t> drained = drainedCells(grid, f);
    out.depressions = depressions.size();
    for (Depression& d : depressions) {
        const double depth = static_cast<double>(f.level[d.outlet] - d.lowest);
        const double area = static_cast<double>(drained[d.outlet]) * grid.cellArea();
        const double cells = static_cast<double>(d.cells) * grid.cellArea();
        // A depression below the sea level never drains: the sea would flow in. Nor one held
        // behind a col: lowering cells never lowers it.
        if (d.lowest < kSeaLevel || d.fill > f.level[d.outlet] ||
            depth > allowedDepth(params, area, d.mountain)) {
            out.keptArea += cells;
            continue;
        }
        ++out.breached;
        out.breachedArea += cells;
        std::sort(d.pits.begin(), d.pits.end(), [&](std::uint32_t a, std::uint32_t b) {
            return lowerCell(height, a, b);
        });
        for (const std::uint32_t pit : d.pits) {
            carveFrom(grid, height, ocean, f, pit, out);
        }
    }
    return out;
}

} // namespace terrain::planet

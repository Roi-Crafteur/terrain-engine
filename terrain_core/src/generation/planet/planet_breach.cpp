#include "terrain/generation/planet/planet_breach.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/planet/planet_land.hpp"
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

// The Priority-Flood: per cell, the direction toward the cell it was flooded from (bits 0-3,
// kNoNeighbor on the ocean) and kBelow when it lies below its fill level; the land cells in flood
// order. The fill levels themselves travel in the front only.
constexpr std::uint8_t kDirection = 0x0F;
constexpr std::uint8_t kUnseen = 0x0F;
constexpr std::uint8_t kBelow = 0x80;

struct Flood {
    std::vector<std::uint8_t> parent;
    std::vector<std::uint32_t> order;

    [[nodiscard]] std::size_t up(const PlanetGrid& grid, std::size_t cell) const {
        return grid.neighbor(cell, parent[cell] & kDirection);
    }
    [[nodiscard]] bool below(std::size_t cell) const { return (parent[cell] & kBelow) != 0; }
};

struct Front {
    float level;
    std::uint32_t cell;
    bool operator>(const Front& other) const {
        return level > other.level || (level == other.level && cell > other.cell);
    }
};

Flood priorityFlood(const PlanetGrid& grid, const std::vector<float>& height,
                    const std::vector<std::uint8_t>& ocean, const LandIndex& land) {
    Flood f;
    f.parent.assign(grid.cells(), kNoNeighbor);
    f.order.reserve(land.landCells());
    land.forEachLand([&f](std::size_t cell, std::size_t) { f.parent[cell] = kUnseen; });
    std::priority_queue<Front, std::vector<Front>, std::greater<>> front;
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
            if (f.parent[n] != kUnseen) {
                continue; // the ocean, or a cell already reached
            }
            const bool below = top.level > height[n];
            f.parent[n] = static_cast<std::uint8_t>(kOpposite[static_cast<std::size_t>(k)] |
                                                    (below ? kBelow : 0));
            front.push({below ? top.level : height[n], static_cast<std::uint32_t>(n)});
        }
    }
    return f;
}

// A closed depression: its outlet and the fill level there (taken before any carving: carving
// another depression may lower the outlet), its lowest height, its cells and whether one of them
// or the outlet lies under the mountains.
struct Depression {
    std::uint32_t outlet = kNone;
    float fill = 0.0f;
    float lowest = 0.0f;
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

// The cells below their fill level with their outlet, the first cell at its fill level on their
// way to the ocean, sorted by outlet then cell.
std::vector<std::pair<std::uint32_t, std::uint32_t>> membersOf(const PlanetGrid& grid,
                                                              const LandIndex& land,
                                                              const Flood& f) {
    std::size_t count = 0;
    for (const std::uint32_t c : f.order) {
        count += f.below(c) ? 1U : 0U;
    }
    std::vector<std::pair<std::uint32_t, std::uint32_t>> members; // outlet, cell
    members.reserve(count);
    std::vector<std::uint32_t> outlet(land.landCells(), kNone);
    for (const std::uint32_t c : f.order) {
        if (!f.below(c)) {
            continue;
        }
        const std::size_t up = f.up(grid, c);
        const std::uint32_t above = land.land(up) ? outlet[land.slot(up)] : kNone;
        const std::uint32_t exit = above != kNone ? above : static_cast<std::uint32_t>(up);
        outlet[land.slot(c)] = exit;
        members.emplace_back(exit, c);
    }
    std::sort(members.begin(), members.end());
    return members;
}

// The depressions, by outlet.
std::vector<Depression> depressionsOf(const PlanetGrid& grid, const std::vector<float>& height,
                                      const std::vector<std::uint8_t>& mountain,
                                      const LandIndex& land, const Flood& f) {
    std::vector<Depression> out;
    for (const auto& [exit, c] : membersOf(grid, land, f)) {
        if (out.empty() || out.back().outlet != exit) {
            // A land outlet is at its own height; the sea at the sea level.
            const float fill = land.land(exit) ? height[exit] : kSeaLevel;
            out.push_back({exit, fill, height[c], 0, mountain[exit] != 0, {}});
        }
        Depression& d = out.back();
        d.lowest = std::min(d.lowest, height[c]);
        ++d.cells;
        d.mountain = d.mountain || mountain[c] != 0;
        if (isPit(grid, height, c)) {
            d.pits.push_back(c);
        }
    }
    return out;
}

// The cells upstream of each land cell in the flood tree, itself included.
std::vector<std::uint32_t> drainedCells(const PlanetGrid& grid, const LandIndex& land,
                                        const Flood& f) {
    std::vector<std::uint32_t> count(land.landCells(), 1);
    for (auto it = f.order.rbegin(); it != f.order.rend(); ++it) {
        const std::size_t up = f.up(grid, *it);
        if (land.land(up)) {
            count[land.slot(up)] += count[land.slot(*it)];
        }
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
    std::size_t c = f.up(grid, pit);
    while (ocean[c] == kLand) {
        target -= kChannelStep;
        if (!(height[c] > target) || target < kSeaLevel) {
            break;
        }
        out.deepestCut = std::max(out.deepestCut, height[c] - target);
        ++out.cutCells;
        height[c] = target;
        c = f.up(grid, c);
    }
}

} // namespace

PlanetBreach breachDepressions(const PlanetGrid& grid, std::vector<float>& height,
                               const std::vector<std::uint8_t>& ocean,
                               const std::vector<std::uint8_t>& mountain,
                               const PlanetBreachParams& params) {
    PlanetBreach out;
    const LandIndex land(ocean);
    const Flood f = priorityFlood(grid, height, ocean, land);
    std::vector<Depression> depressions = depressionsOf(grid, height, mountain, land, f);
    const std::vector<std::uint32_t> drained = drainedCells(grid, land, f);
    out.depressions = depressions.size();
    for (Depression& d : depressions) {
        const double depth = static_cast<double>(d.fill - d.lowest);
        const double cells = static_cast<double>(d.cells) * grid.cellArea();
        // A depression below the sea level never drains: the sea would flow in. Every depression
        // flooded from the ocean is one (its cells lie below the sea level that filled them), so
        // the outlet of the others is on land.
        const bool sealed = d.lowest < kSeaLevel;
        const double area =
            sealed ? 0.0
                   : static_cast<double>(drained[land.slot(d.outlet)]) * grid.cellArea();
        if (sealed || depth > allowedDepth(params, area, d.mountain)) {
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

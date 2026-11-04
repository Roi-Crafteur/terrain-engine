// The window of a block and the ways of its trunks (network_window.hpp).
#include "network_window.hpp"

#include <algorithm>
#include <limits>
#include <queue>
#include <tuple>

namespace terrain::water {

namespace {

constexpr std::uint32_t kNoTrunk = 0xFFFFFFFFu;

// The window position (column, row of cells) of a cell of the grid, or false outside the window.
bool cellInWindow(const PlanetGrid& grid, const NetworkWindow& w, std::size_t cell, int& a,
                  int& b) {
    const auto wrap = [](int d, int n) { return ((d % n) + n) % n; };
    a = wrap(grid.column(cell) - (w.ci - w.radius), grid.nx);
    b = wrap(grid.row(cell) - (w.cj - w.radius), grid.nz);
    const int cells = 2 * w.radius + 1;
    return a < cells && b < cells;
}

// The lowest node of the window cell (a, b) (by height, then index).
std::size_t lowestNode(const NetworkWindow& w, int a, int b) {
    std::size_t best = w.index(a * w.perCell, b * w.perCell);
    for (int y = 0; y < w.perCell; ++y) {
        for (int x = 0; x < w.perCell; ++x) {
            const std::size_t k = w.index(a * w.perCell + x, b * w.perCell + y);
            if (w.height[k] < w.height[best] || (w.height[k] == w.height[best] && k < best)) {
                best = k;
            }
        }
    }
    return best;
}

// The lowest way from node from to node to over the nodes of the cells of the window (a0, b0) and
// (a1, b1): the least of the highest heights, then the fewest steps (Dijkstra).
std::vector<std::size_t> lowestWay(const NetworkWindow& w, std::size_t start, std::size_t to,
                                   int a0, int b0, int a1, int b1) {
    const auto inCells = [&](int x, int y) {
        const int a = x / w.perCell;
        const int b = y / w.perCell;
        return (a == a0 && b == b0) || (a == a1 && b == b1);
    };
    using Key = std::tuple<float, int, std::size_t>;
    std::priority_queue<Key, std::vector<Key>, std::greater<>> front;
    std::vector<float> best(w.count(), std::numeric_limits<float>::infinity());
    std::vector<std::size_t> came(w.count(), static_cast<std::size_t>(-1));
    best[start] = w.height[start];
    front.emplace(best[start], 0, start);
    while (!front.empty()) {
        const auto [level, steps, k] = front.top();
        front.pop();
        if (level > best[k] || k == to) {
            if (k == to) {
                break;
            }
            continue;
        }
        const int x = static_cast<int>(k % static_cast<std::size_t>(w.side));
        const int y = static_cast<int>(k / static_cast<std::size_t>(w.side));
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int nx = x + dx;
                const int ny = y + dy;
                if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= w.side || ny >= w.side ||
                    !inCells(nx, ny)) {
                    continue;
                }
                const std::size_t n = w.index(nx, ny);
                const float reach = std::max(level, w.height[n]);
                if (reach < best[n]) {
                    best[n] = reach;
                    came[n] = k;
                    front.emplace(reach, steps + 1, n);
                }
            }
        }
    }
    std::vector<std::size_t> way;
    for (std::size_t k = to; k != static_cast<std::size_t>(-1); k = came[k]) {
        way.push_back(k);
    }
    return way;
}

} // namespace

NetworkWindow networkWindow(const PlanetGrid& grid, const CellNodesOf& nodesOf, int ci, int cj,
                            int radius, int perCell) {
    NetworkWindow w;
    w.radius = radius;
    w.perCell = perCell;
    w.ci = ci;
    w.cj = cj;
    w.side = (2 * radius + 1) * perCell;
    w.height.resize(w.count());
    w.sea.resize(w.count());
    w.permeability.resize(w.count());
    w.cell.resize(w.count());
    for (int b = 0; b < 2 * radius + 1; ++b) {
        for (int a = 0; a < 2 * radius + 1; ++a) {
            const int gi = grid.wrapColumn(ci - radius + a);
            const int gj = grid.wrapRow(cj - radius + b);
            const std::shared_ptr<const CellNodes> nodes = nodesOf(gi, gj);
            for (int y = 0; y < perCell; ++y) {
                for (int x = 0; x < perCell; ++x) {
                    const auto from = static_cast<std::size_t>(y * perCell + x);
                    const std::size_t k = w.index(a * perCell + x, b * perCell + y);
                    w.height[k] = nodes->height[from];
                    w.sea[k] = nodes->sea[from];
                    w.permeability[k] = nodes->permeability[from];
                    w.cell[k] = static_cast<std::uint32_t>(grid.index(gi, gj));
                }
            }
        }
    }
    return w;
}

TrunkNodes trunkNodes(const PlanetNetwork& network, const NetworkWindow& w) {
    const PlanetGrid& grid = network.level().grid();
    TrunkNodes trunk{std::vector<std::uint32_t>(w.count(), kNoTrunk),
                     std::vector<std::size_t>(w.count(), static_cast<std::size_t>(-1))};
    const int cells = 2 * w.radius + 1;
    for (int b = 0; b < cells; ++b) {
        for (int a = 0; a < cells; ++a) {
            const std::size_t cell = w.cell[w.index(a * w.perCell, b * w.perCell)];
            const std::int64_t down = network.down(cell);
            int a1 = 0;
            int b1 = 0;
            if (!network.trunk(cell) || down < 0 ||
                !cellInWindow(grid, w, static_cast<std::size_t>(down), a1, b1)) {
                continue;
            }
            // The way from its end back to its start: each node's next is the one before it.
            const std::vector<std::size_t> way =
                lowestWay(w, lowestNode(w, a, b), lowestNode(w, a1, b1), a, b, a1, b1);
            for (std::size_t i = 0; i < way.size(); ++i) {
                const std::size_t k = way[i];
                if (static_cast<std::uint32_t>(cell) < trunk.cell[k]) {
                    trunk.cell[k] = static_cast<std::uint32_t>(cell);
                    trunk.next[k] = i > 0 ? way[i - 1] : static_cast<std::size_t>(-1);
                }
            }
        }
    }
    return trunk;
}

} // namespace terrain::water

#include "network_block.hpp"

#include "network_window.hpp"
#include "terrain/generation/planet/planet_climate.hpp"

#include <algorithm>
#include <cmath>
#include <queue>
#include <utility>

namespace terrain::water {

namespace {

constexpr std::uint32_t kNoTrunkCell = 0xFFFFFFFFu;
constexpr std::size_t kNone = static_cast<std::size_t>(-1);

// The outlets of the window and their levels: the sea, the lakes of the planet level, the trunks
// and the edge.
struct Outlets {
    std::vector<NodeOutlet> kind;
    std::vector<float> level;
    TrunkNodes trunk;
};

Outlets outletsOf(const PlanetNetwork& network, const NetworkWindow& w) {
    const PlanetLevel& level = network.level();
    Outlets o{std::vector<NodeOutlet>(w.count(), NodeOutlet::None), w.height, trunkNodes(network, w)};
    for (int y = 0; y < w.side; ++y) {
        for (int x = 0; x < w.side; ++x) {
            const std::size_t k = w.index(x, y);
            const std::uint32_t cell = w.cell[k];
            if (w.sea[k] != 0) {
                o.kind[k] = NodeOutlet::Sea;
            } else if ((level.cells().bits[cell] & PlanetCells::kLakeBit) != 0 &&
                       w.height[k] < level.lakes()[static_cast<std::size_t>(
                                         level.cells().lakeAt(cell))].level) {
                o.kind[k] = NodeOutlet::Lake;
                o.level[k] = level.lakes()[static_cast<std::size_t>(level.cells().lakeAt(cell))].level;
            } else if (o.trunk.cell[k] != kNoTrunkCell) {
                o.kind[k] = NodeOutlet::Trunk;
            } else if (x == 0 || y == 0 || x == w.side - 1 || y == w.side - 1) {
                o.kind[k] = NodeOutlet::Edge;
            }
        }
    }
    return o;
}

// The flood from the outlets: each node reached from the node below it, in the order of the
// filled levels (then of the indices); the order of the nodes, outlets first.
struct Flood {
    std::vector<std::size_t> down;
    std::vector<float> filled;
    std::vector<std::size_t> order;
};

Flood floodOf(const NetworkWindow& w, const Outlets& o) {
    using Key = std::pair<float, std::size_t>;
    std::priority_queue<Key, std::vector<Key>, std::greater<>> front;
    Flood f{std::vector<std::size_t>(w.count(), kNone), o.level, {}};
    std::vector<std::uint8_t> seen(w.count(), 0);
    for (std::size_t k = 0; k < w.count(); ++k) {
        if (o.kind[k] != NodeOutlet::None) {
            seen[k] = 1;
            front.emplace(f.filled[k], k);
        }
    }
    f.order.reserve(w.count());
    while (!front.empty()) {
        const auto [level, k] = front.top();
        front.pop();
        f.order.push_back(k);
        const int x = static_cast<int>(k % static_cast<std::size_t>(w.side));
        const int y = static_cast<int>(k / static_cast<std::size_t>(w.side));
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int nx = x + dx;
                const int ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= w.side || ny >= w.side) {
                    continue;
                }
                const std::size_t n = w.index(nx, ny);
                if (seen[n] != 0) {
                    continue;
                }
                seen[n] = 1;
                f.down[n] = k;
                f.filled[n] = std::max(w.height[n], level);
                front.emplace(f.filled[n], n);
            }
        }
    }
    return f;
}

// The climate the channel heads read in a cell.
RunoffSite siteOf(const PlanetLevel& level, std::uint32_t cell) {
    const PlanetGrid& grid = level.grid();
    RunoffSite site;
    site.precipitation = level.precipitation(cell);
    site.temperature = level.temperature(cell);
    site.pet = static_cast<float>(
        planet::potentialEvapotranspiration(site.temperature, level.params().climate));
    site.latitude =
        static_cast<float>(180.0 * std::abs(grid.centerZ(grid.row(cell))) / grid.periodZ());
    return site;
}

// What the nodes of the window gather, upstream first: the area, the flow, the channels and their
// orders, the largest donor of each, the share of the yield that runs off.
struct Gathered {
    std::vector<double> area;
    std::vector<double> flow;
    std::vector<std::uint8_t> channel;
    std::vector<std::uint8_t> order;
    std::vector<std::size_t> mainDonor;
    std::vector<float> surface;
};

// The order of a channel from its inputs (the highest, and whether two reach it), 1 at its head.
std::uint8_t channelOrder(std::uint8_t most, std::uint8_t twice) {
    return most == 0 ? 1 : static_cast<std::uint8_t>(twice != 0 ? most + 1 : most);
}

Gathered gather(const PlanetLevel& level, const NetworkWindow& w, const Outlets& o, const Flood& f) {
    const double nodeArea = kNodeSpacing * kNodeSpacing;
    const double cellArea = level.grid().cellArea();
    Gathered g{std::vector<double>(w.count(), 0.0), std::vector<double>(w.count(), 0.0),
               std::vector<std::uint8_t>(w.count(), 0), std::vector<std::uint8_t>(w.count(), 0),
               std::vector<std::size_t>(w.count(), kNone), std::vector<float>(w.count(), 0.0f)};
    std::vector<std::uint8_t> most(w.count(), 0);
    std::vector<std::uint8_t> twice(w.count(), 0);
    const RunoffParams runoff;
    for (auto it = f.order.rbegin(); it != f.order.rend(); ++it) {
        const std::size_t k = *it;
        const std::uint32_t cell = w.cell[k];
        const double yield =
            static_cast<double>(level.water(cell).runoff) * 1.0e-6 / cellArea; // m^3/s per m^2
        g.area[k] += nodeArea;
        g.flow[k] += nodeArea * yield;
        const std::size_t d = f.down[k];
        RunoffSite site = siteOf(level, cell);
        site.permeability = w.permeability[k];
        const double drop = d != kNone ? static_cast<double>(w.height[k] - w.height[d]) : 0.0;
        site.slope = static_cast<float>(std::max(drop, 0.0) / kNodeSpacing);
        const RunoffShare share = partitionRunoff(site, runoff);
        g.surface[k] = share.surface;
        if (o.kind[k] == NodeOutlet::None && g.area[k] >= share.headArea) {
            g.channel[k] = 1;
        }
        g.order[k] = g.channel[k] != 0 ? channelOrder(most[k], twice[k]) : 0;
        if (d == kNone) {
            continue;
        }
        g.area[d] += g.area[k];
        g.flow[d] += g.flow[k];
        if (g.channel[k] != 0) {
            g.channel[d] = o.kind[d] == NodeOutlet::None ? 1 : g.channel[d];
            twice[d] = g.order[k] > most[d] ? 0 : (g.order[k] == most[d] ? 1 : twice[d]);
            most[d] = std::max(most[d], g.order[k]);
            if (g.mainDonor[d] == kNone || g.flow[k] > g.flow[g.mainDonor[d]]) {
                g.mainDonor[d] = k;
            }
        }
    }
    return g;
}

// The step from window node from to window node to, or kNoStep.
void stepOf(const NetworkWindow& w, std::size_t from, std::size_t to, std::int8_t& sx,
            std::int8_t& sz) {
    if (to == kNone) {
        return;
    }
    const auto side = static_cast<std::size_t>(w.side);
    sx = static_cast<std::int8_t>(static_cast<int>(to % side) - static_cast<int>(from % side));
    sz = static_cast<std::int8_t>(static_cast<int>(to / side) - static_cast<int>(from / side));
}

} // namespace

NetworkBlock computeBlock(const PlanetNetwork& network, const NodeLattice& lattice,
                          const CellNodesOf& nodesOf, int ci, int cj, int radius) {
    const PlanetLevel& level = network.level();
    const NetworkWindow w = networkWindow(level.grid(), nodesOf, ci, cj, radius, lattice.perCell);
    const Outlets o = outletsOf(network, w);
    const Flood f = floodOf(w, o);
    const Gathered g = gather(level, w, o, f);
    const RunoffParams runoff;
    NetworkBlock block;
    block.ci = ci;
    block.cj = cj;
    const int n = lattice.perCell;
    block.nodes.resize(static_cast<std::size_t>(n) * static_cast<std::size_t>(n));
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const std::size_t k = w.index(radius * n + x, radius * n + y);
            NetworkNode& node = block.nodes[static_cast<std::size_t>(y * n + x)];
            stepOf(w, k, f.down[k], node.downX, node.downZ);
            stepOf(w, k, g.mainDonor[k], node.mainX, node.mainZ);
            node.outlet = o.kind[k];
            node.order = g.order[k];
            node.channel = g.channel[k] != 0;
            node.wet = node.channel && channelWet(g.flow[k], g.surface[k], runoff);
            node.height = w.height[k];
            node.filled = f.filled[k];
            node.area = static_cast<float>(g.area[k]);
            node.discharge = static_cast<float>(g.flow[k]);
            node.surface = g.surface[k];
            node.permeability = w.permeability[k];
            node.trunkCell = o.trunk.cell[k];
            if (node.outlet == NodeOutlet::Trunk) {
                stepOf(w, k, o.trunk.next[k], node.downX, node.downZ);
            }
        }
    }
    return block;
}

} // namespace terrain::water

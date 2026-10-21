#include "terrain/generation/planet/planet_lakes.hpp"

#include "terrain/generation/planet/planet_relief.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

namespace terrain::planet {

namespace {

// Prefix sums over the positions of the leaves, with point updates.
class Fenwick {
public:
    explicit Fenwick(std::size_t size) : tree_(size + 1, 0) {}

    void add(std::size_t position, std::int64_t value) {
        for (std::size_t i = position + 1; i < tree_.size(); i += i & (~i + 1)) {
            tree_[i] += value;
        }
    }
    [[nodiscard]] std::int64_t range(std::size_t begin, std::size_t end) const {
        return prefix(end) - prefix(begin);
    }

private:
    [[nodiscard]] std::int64_t prefix(std::size_t end) const {
        std::int64_t sum = 0;
        for (std::size_t i = end; i > 0; i -= i & (~i + 1)) {
            sum += tree_[i];
        }
        return sum;
    }

    std::vector<std::int64_t> tree_;
};

// Contiguous positions of the leaves of every subtree: [start, start + count).
struct LeafRanges {
    std::vector<std::size_t> start;
    std::vector<std::size_t> count;
    std::size_t leaves = 0;
};

LeafRanges leafRanges(const DepressionHierarchy& hierarchy) {
    const std::size_t nodes = hierarchy.nodes.size();
    LeafRanges ranges;
    ranges.start.assign(nodes, 0);
    ranges.count.assign(nodes, 0);
    for (std::size_t n = 1; n < nodes; ++n) {
        const DepressionNode& node = hierarchy.nodes[n];
        ranges.count[n] = node.leaf()
                              ? 1
                              : ranges.count[static_cast<std::size_t>(node.children[0])] +
                                    ranges.count[static_cast<std::size_t>(node.children[1])];
        if (node.parent == kOceanNode) {
            ranges.start[n] = ranges.leaves;
            ranges.leaves += ranges.count[n];
        }
    }
    for (std::size_t n = nodes; n-- > 1;) {
        const DepressionNode& node = hierarchy.nodes[n];
        if (!node.leaf()) {
            const auto a = static_cast<std::size_t>(node.children[0]);
            ranges.start[a] = ranges.start[n];
            ranges.start[static_cast<std::size_t>(node.children[1])] =
                ranges.start[n] + ranges.count[a];
        }
    }
    return ranges;
}

class Solver {
public:
    Solver(const std::vector<float>& height, const DepressionHierarchy& hierarchy,
           LakeSolution& solution)
        : height_(height), hierarchy_(hierarchy), solution_(solution),
          ranges_(leafRanges(hierarchy)), water_(ranges_.leaves) {}

    void gather(const PlanetGrid& grid, const WaterOf& water);
    void solveAll();

private:
    [[nodiscard]] std::int64_t inflowOf(std::size_t n) const {
        return water_.range(ranges_.start[n], ranges_.start[n] + ranges_.count[n]);
    }
    void deliver(std::int32_t leaf, std::int64_t amount);
    void solveTree(std::int32_t root);
    void solveNode(std::size_t n, std::int64_t inflow);

    const std::vector<float>& height_;
    const DepressionHierarchy& hierarchy_;
    LakeSolution& solution_;
    LeafRanges ranges_;
    Fenwick water_;
};

void Solver::gather(const PlanetGrid& grid, const WaterOf& water) {
    const std::size_t nodes = hierarchy_.nodes.size();
    std::vector<std::int64_t> own(nodes, 0);
    for (std::size_t c = 0; c < grid.cells(); ++c) {
        const CellWater cell = water(c);
        solution_.runoff += cell.runoff;
        deliver(hierarchy_.label[c], cell.runoff);
        own[static_cast<std::size_t>(hierarchy_.own[c])] += cell.lakeEvaporation;
    }
    solution_.capacity.assign(nodes, 0);
    for (std::size_t n = 1; n < nodes; ++n) {
        const DepressionNode& node = hierarchy_.nodes[n];
        solution_.capacity[n] = own[n];
        if (!node.leaf()) {
            solution_.capacity[n] +=
                solution_.capacity[static_cast<std::size_t>(node.children[0])] +
                solution_.capacity[static_cast<std::size_t>(node.children[1])];
        }
    }
}

void Solver::deliver(std::int32_t leaf, std::int64_t amount) {
    if (leaf == kOceanNode) {
        solution_.toOcean += amount;
    } else {
        water_.add(ranges_.start[static_cast<std::size_t>(leaf)], amount);
    }
}

void Solver::solveAll() {
    const std::size_t nodes = hierarchy_.nodes.size();
    solution_.state.assign(nodes, DepressionState::Dry);
    solution_.state[0] = DepressionState::Full;
    solution_.inflow.assign(nodes, 0);
    std::vector<std::int32_t> roots;
    for (std::size_t n = 1; n < nodes; ++n) {
        if (hierarchy_.nodes[n].parent == kOceanNode) {
            roots.push_back(static_cast<std::int32_t>(n));
        }
    }
    // Highest pass first: a root only ever sends water to a tree attached at a lower pass.
    std::sort(roots.begin(), roots.end(), [this](std::int32_t a, std::int32_t b) {
        return lowerCell(height_, hierarchy_.nodes[static_cast<std::size_t>(b)].pass,
                         hierarchy_.nodes[static_cast<std::size_t>(a)].pass);
    });
    for (const std::int32_t root : roots) {
        solveTree(root);
    }
}

void Solver::solveTree(std::int32_t root) {
    std::vector<std::int32_t> stack{root};
    while (!stack.empty()) {
        const auto n = static_cast<std::size_t>(stack.back());
        stack.pop_back();
        const std::int64_t inflow = inflowOf(n);
        solveNode(n, inflow);
        const DepressionNode& node = hierarchy_.nodes[n];
        if (!node.leaf()) {
            stack.push_back(node.children[0]);
            stack.push_back(node.children[1]);
        }
        if (static_cast<std::int32_t>(n) == root && solution_.state[n] == DepressionState::Full) {
            deliver(node.geolink, inflow - solution_.capacity[n]);
        }
    }
}

void Solver::solveNode(std::size_t n, std::int64_t inflow) {
    const DepressionNode& node = hierarchy_.nodes[n];
    const std::vector<std::int64_t>& capacity = solution_.capacity;
    solution_.inflow[n] = inflow;
    std::int64_t children = 0;
    if (!node.leaf()) {
        const auto a = static_cast<std::size_t>(node.children[0]);
        const auto b = static_cast<std::size_t>(node.children[1]);
        const std::int64_t inA = inflowOf(a);
        const std::int64_t inB = inflowOf(b);
        const bool fullA = inA > capacity[a];
        const bool fullB = inB > capacity[b];
        // Only one child full by itself: its excess runs into the other one.
        if (fullA && !fullB) {
            deliver(hierarchy_.nodes[a].geolink, inA - capacity[a]);
        } else if (fullB && !fullA) {
            deliver(hierarchy_.nodes[b].geolink, inB - capacity[b]);
        }
        children = capacity[a] + capacity[b];
    }
    if (inflow > capacity[n]) {
        solution_.state[n] = DepressionState::Full;
    } else if (inflow > children || (node.leaf() && inflow > 0)) {
        solution_.state[n] = DepressionState::Partial;
    }
}

// The pit of the leftmost leaf of a node: the lowest cell of the first depression it merged.
std::uint32_t lowestPit(const DepressionHierarchy& hierarchy, std::size_t n) {
    while (!hierarchy.nodes[n].leaf()) {
        n = static_cast<std::size_t>(hierarchy.nodes[n].children[0]);
    }
    return hierarchy.nodes[n].pit;
}

void assignLakes(const std::vector<float>& height, const DepressionHierarchy& hierarchy,
                 LakeSolution& solution) {
    const std::size_t nodes = hierarchy.nodes.size();
    const auto wet = [&](std::int32_t n) {
        return n != kOceanNode &&
               solution.state[static_cast<std::size_t>(n)] != DepressionState::Dry;
    };
    solution.lakeOfNode.assign(nodes, -1);
    for (std::size_t n = 1; n < nodes; ++n) {
        const DepressionNode& node = hierarchy.nodes[n];
        if (!wet(static_cast<std::int32_t>(n)) || wet(node.parent)) {
            continue;
        }
        solution.lakeOfNode[n] = static_cast<std::int32_t>(solution.lakes.size());
        PlanetLake lake;
        lake.node = static_cast<std::int32_t>(n);
        lake.overflowing = solution.state[n] == DepressionState::Full;
        lake.pit = lowestPit(hierarchy, n);
        lake.pass = node.pass;
        lake.partner = node.partner;
        lake.level = height[node.pass];
        lake.inflow = solution.inflow[n];
        lake.evaporation = lake.overflowing ? solution.capacity[n] : solution.inflow[n];
        lake.outflow = lake.inflow - lake.evaporation;
        solution.lakes.push_back(lake);
    }
    for (std::size_t n = nodes; n-- > 1;) {
        const DepressionNode& node = hierarchy.nodes[n];
        if (solution.lakeOfNode[n] < 0 && wet(static_cast<std::int32_t>(n))) {
            solution.lakeOfNode[n] = solution.lakeOfNode[static_cast<std::size_t>(node.parent)];
        }
    }
}

// Cells under water, the levels of the partial lakes, and the extent of every lake.
void submerge(const PlanetGrid& grid, const std::vector<float>& height,
              const DepressionHierarchy& hierarchy, const std::vector<std::uint32_t>& order,
              const WaterOf& water, LakeSolution& solution) {
    const std::size_t nodes = hierarchy.nodes.size();
    std::vector<std::int64_t> remaining(nodes, -1);
    for (const PlanetLake& lake : solution.lakes) {
        const auto n = static_cast<std::size_t>(lake.node);
        if (!lake.overflowing) {
            const DepressionNode& node = hierarchy.nodes[n];
            remaining[n] = solution.inflow[n];
            if (!node.leaf()) {
                remaining[n] -= solution.capacity[static_cast<std::size_t>(node.children[0])] +
                                solution.capacity[static_cast<std::size_t>(node.children[1])];
            }
        }
    }
    solution.lakeOfCell.assign(grid.cells(), -1);
    for (const std::uint32_t c : order) {
        const auto own = static_cast<std::size_t>(hierarchy.own[c]);
        const DepressionState state = solution.state[own];
        if (own == 0 || state == DepressionState::Dry) {
            continue;
        }
        if (state == DepressionState::Full) {
            solution.lakeOfCell[c] = solution.lakeOfNode[own];
            continue;
        }
        if (remaining[own] < 0) {
            continue; // the lake stopped below this cell
        }
        const std::int64_t evaporation = water(c).lakeEvaporation;
        PlanetLake& lake = solution.lakes[static_cast<std::size_t>(solution.lakeOfNode[own])];
        if (evaporation <= remaining[own]) {
            remaining[own] -= evaporation;
            solution.lakeOfCell[c] = solution.lakeOfNode[own];
        } else {
            remaining[own] = -1;
            lake.level = height[c];
        }
    }
    std::vector<std::uint32_t> frozen(solution.lakes.size(), 0);
    for (std::size_t c = 0; c < grid.cells(); ++c) {
        const std::int32_t id = solution.lakeOfCell[c];
        if (id >= 0) {
            PlanetLake& lake = solution.lakes[static_cast<std::size_t>(id)];
            ++lake.cells;
            lake.volume +=
                std::max(0.0, static_cast<double>(lake.level) - height[c]) * grid.cellArea();
            frozen[static_cast<std::size_t>(id)] += water(c).frozen ? 1u : 0u;
        }
    }
    for (std::size_t l = 0; l < solution.lakes.size(); ++l) {
        PlanetLake& lake = solution.lakes[l];
        lake.playa = lake.cells == 0;
        lake.frozen = 2 * frozen[l] > lake.cells;
        if (lake.playa) {
            lake.level = height[lake.pit];
        }
    }
}

} // namespace

LakeSolution solveLakes(const PlanetGrid& grid, const std::vector<float>& height,
                        const DepressionHierarchy& hierarchy,
                        const std::vector<std::uint32_t>& order, const WaterOf& water) {
    LakeSolution solution;
    Solver solver(height, hierarchy, solution);
    solver.gather(grid, water);
    solver.solveAll();
    assignLakes(height, hierarchy, solution);
    submerge(grid, height, hierarchy, order, water, solution);
    return solution;
}

} // namespace terrain::planet

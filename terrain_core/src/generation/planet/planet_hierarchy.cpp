#include "terrain/generation/planet/planet_hierarchy.hpp"

#include "terrain/generation/planet/planet_relief.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace terrain::planet {

namespace {

// Union-find over the cells: a root holds -(node + 1), any other cell its parent cell.
class FloodSets {
public:
    explicit FloodSets(std::size_t cells) : parent_(cells, 0) {}

    std::int32_t root(std::int32_t cell) {
        while (parent_[static_cast<std::size_t>(cell)] >= 0) {
            const std::int32_t up = parent_[static_cast<std::size_t>(cell)];
            const std::int32_t grand = parent_[static_cast<std::size_t>(up)];
            if (grand >= 0) {
                parent_[static_cast<std::size_t>(cell)] = grand; // path halving
            }
            cell = parent_[static_cast<std::size_t>(cell)];
        }
        return cell;
    }
    std::int32_t node(std::int32_t root) const {
        return -parent_[static_cast<std::size_t>(root)] - 1;
    }
    void makeRoot(std::size_t cell, std::int32_t node) { parent_[cell] = -(node + 1); }
    void link(std::size_t cell, std::int32_t parent) { parent_[cell] = parent; }
    std::vector<std::int32_t> release() { return std::move(parent_); }

private:
    std::vector<std::int32_t> parent_;
};

// The distinct flooded sets around a cell, in the order of kPlanetNeighbors, each with the lowest
// neighbor of the cell in it.
struct Around {
    std::array<std::int32_t, 8> roots{};
    std::array<std::uint32_t, 8> lowest{};
    int count = 0;
};

Around floodedAround(const PlanetGrid& grid, const std::vector<float>& height,
                     const std::vector<std::uint8_t>& ocean, const PlanetCols* cols,
                     std::size_t cell, FloodSets& sets) {
    Around around;
    for (int k = 0; k < 8; ++k) {
        const std::size_t n = grid.neighbor(cell, k);
        if (ocean[n] == kLand &&
            (!lowerCell(height, n, cell) || (cols != nullptr && cols->held(height, cell, k)))) {
            continue; // not flooded yet (the ocean always is, being lower), or held by a col
        }
        const std::int32_t root = sets.root(static_cast<std::int32_t>(n));
        int slot = 0;
        while (slot < around.count && around.roots[static_cast<std::size_t>(slot)] != root) {
            ++slot;
        }
        const auto s = static_cast<std::size_t>(slot);
        if (slot == around.count) {
            around.roots[s] = root;
            around.lowest[s] = static_cast<std::uint32_t>(n);
            ++around.count;
        } else if (lowerCell(height, n, around.lowest[s])) {
            around.lowest[s] = static_cast<std::uint32_t>(n);
        }
    }
    return around;
}

// An edge whose col holds the water: from cell toward its neighbor k, at level.
struct ColEvent {
    float level;
    std::uint32_t cell;
    std::uint8_t k;
};

// The edges of the land held by their cols, lowest first (then by cell and direction).
std::vector<ColEvent> colEvents(const PlanetGrid& grid, const std::vector<float>& height,
                                const std::vector<std::uint8_t>& ocean, const PlanetCols& cols) {
    std::vector<ColEvent> events;
    for (std::size_t c = 0; c < grid.cells(); ++c) {
        if (ocean[c] != kLand) {
            continue;
        }
        for (const int k : {0, 2, 4, 5}) { // every edge once: east, south and the diagonals south
            float level = 0.0f;
            if (ocean[grid.neighbor(c, k)] == kLand && cols.held(height, c, k, &level)) {
                events.push_back({level, static_cast<std::uint32_t>(c), static_cast<std::uint8_t>(k)});
            }
        }
    }
    std::sort(events.begin(), events.end(), [](const ColEvent& a, const ColEvent& b) {
        return a.level < b.level || (a.level == b.level && (a.cell < b.cell ||
                                                            (a.cell == b.cell && a.k < b.k)));
    });
    return events;
}

class Sweep {
public:
    Sweep(const PlanetGrid& grid, const std::vector<float>& height,
          const std::vector<std::uint8_t>& ocean, const PlanetCols* cols,
          DepressionHierarchy& result)
        : grid_(grid), height_(height), ocean_(ocean), cols_(cols), result_(result),
          sets_(grid.cells()) {}

    void joinOcean();
    void flood(std::size_t cell);
    void overCol(const ColEvent& col);
    std::vector<std::int32_t> release() { return sets_.release(); }

private:
    void mergeAll(std::size_t cell, const Around& around);
    void attachToOcean(std::size_t cell, const Around& around, int oceanSlot);

    const PlanetGrid& grid_;
    const std::vector<float>& height_;
    const std::vector<std::uint8_t>& ocean_;
    const PlanetCols* cols_;
    DepressionHierarchy& result_;
    FloodSets sets_;
    std::int32_t oceanRoot_ = -1;
};

void Sweep::joinOcean() {
    result_.nodes.assign(1, DepressionNode{}); // the ocean
    for (std::size_t c = 0; c < grid_.cells(); ++c) {
        if (ocean_[c] == kLand) {
            continue;
        }
        result_.own[c] = kOceanNode;
        if (oceanRoot_ < 0) {
            oceanRoot_ = static_cast<std::int32_t>(c);
            sets_.makeRoot(c, kOceanNode);
        } else {
            sets_.link(c, oceanRoot_);
        }
    }
}

void Sweep::flood(std::size_t cell) {
    const Around around = floodedAround(grid_, height_, ocean_, cols_, cell, sets_);
    if (around.count == 0) {
        const auto leaf = static_cast<std::int32_t>(result_.nodes.size());
        DepressionNode node;
        node.pit = static_cast<std::uint32_t>(cell);
        result_.nodes.push_back(node);
        sets_.makeRoot(cell, leaf);
        result_.own[cell] = leaf;
        return;
    }
    if (around.count == 1) {
        sets_.link(cell, around.roots[0]);
        result_.own[cell] = sets_.node(around.roots[0]);
        return;
    }
    for (int s = 0; s < around.count; ++s) {
        if (sets_.node(around.roots[static_cast<std::size_t>(s)]) == kOceanNode) {
            attachToOcean(cell, around, s);
            return;
        }
    }
    mergeAll(cell, around);
}

void Sweep::attachToOcean(std::size_t cell, const Around& around, int oceanSlot) {
    const std::uint32_t partner = around.lowest[static_cast<std::size_t>(oceanSlot)];
    for (int s = 0; s < around.count; ++s) {
        if (s == oceanSlot) {
            continue;
        }
        const std::int32_t root = around.roots[static_cast<std::size_t>(s)];
        DepressionNode& node = result_.nodes[static_cast<std::size_t>(sets_.node(root))];
        node.parent = kOceanNode;
        node.pass = static_cast<std::uint32_t>(cell);
        node.partner = partner;
        node.level = height_[cell];
        sets_.link(static_cast<std::size_t>(root), oceanRoot_);
    }
    sets_.link(cell, oceanRoot_);
    result_.own[cell] = kOceanNode;
}

void Sweep::mergeAll(std::size_t cell, const Around& around) {
    const std::int32_t keep = around.roots[0];
    std::int32_t current = sets_.node(keep);
    std::uint32_t currentLowest = around.lowest[0];
    for (int s = 1; s < around.count; ++s) {
        const std::int32_t root = around.roots[static_cast<std::size_t>(s)];
        const std::int32_t other = sets_.node(root);
        const std::uint32_t otherLowest = around.lowest[static_cast<std::size_t>(s)];
        const auto merged = static_cast<std::int32_t>(result_.nodes.size());
        DepressionNode node;
        node.children = {current, other};
        result_.nodes.push_back(node);
        for (const auto& [child, partner] :
             {std::pair{current, otherLowest}, std::pair{other, currentLowest}}) {
            DepressionNode& c = result_.nodes[static_cast<std::size_t>(child)];
            c.parent = merged;
            c.pass = static_cast<std::uint32_t>(cell);
            c.partner = partner;
            c.level = height_[cell];
        }
        sets_.link(static_cast<std::size_t>(root), keep);
        sets_.makeRoot(static_cast<std::size_t>(keep), merged);
        current = merged;
        if (lowerCell(height_, otherLowest, currentLowest)) {
            currentLowest = otherLowest;
        }
    }
    sets_.link(cell, keep);
    result_.own[cell] = current;
}

// The water reaches the level of a col: the sets of its two cells merge there, each passing at
// its cell toward the other, or the one apart from the ocean attaches to it.
void Sweep::overCol(const ColEvent& col) {
    const std::uint32_t a = col.cell;
    const auto b = static_cast<std::uint32_t>(grid_.neighbor(a, col.k));
    const std::int32_t ra = sets_.root(static_cast<std::int32_t>(a));
    const std::int32_t rb = sets_.root(static_cast<std::int32_t>(b));
    if (ra == rb) {
        return;
    }
    const std::int32_t na = sets_.node(ra);
    const std::int32_t nb = sets_.node(rb);
    if (na == kOceanNode || nb == kOceanNode) {
        const bool aSide = nb == kOceanNode;
        DepressionNode& node = result_.nodes[static_cast<std::size_t>(aSide ? na : nb)];
        node.parent = kOceanNode;
        node.pass = aSide ? a : b;
        node.partner = aSide ? b : a;
        node.level = col.level;
        sets_.link(static_cast<std::size_t>(aSide ? ra : rb), oceanRoot_);
        return;
    }
    const auto merged = static_cast<std::int32_t>(result_.nodes.size());
    DepressionNode node;
    node.children = {na, nb};
    result_.nodes.push_back(node);
    for (const auto& [child, pass, partner] :
         {std::tuple{na, a, b}, std::tuple{nb, b, a}}) {
        DepressionNode& c = result_.nodes[static_cast<std::size_t>(child)];
        c.parent = merged;
        c.pass = pass;
        c.partner = partner;
        c.level = col.level;
    }
    sets_.link(static_cast<std::size_t>(rb), ra);
    sets_.makeRoot(static_cast<std::size_t>(ra), merged);
}

} // namespace

DepressionHierarchy buildHierarchy(const PlanetGrid& grid, const std::vector<float>& height,
                                   const std::vector<std::uint8_t>& ocean,
                                   const std::vector<std::uint32_t>& order,
                                   const std::vector<std::uint8_t>& receiver,
                                   const PlanetCols* cols) {
    DepressionHierarchy result;
    result.own.assign(grid.cells(), kOceanNode);
    Sweep sweep(grid, height, ocean, cols, result);
    sweep.joinOcean();
    const std::vector<ColEvent> events =
        cols != nullptr ? colEvents(grid, height, ocean, *cols) : std::vector<ColEvent>{};
    std::size_t next = 0;
    for (const std::uint32_t cell : order) {
        for (; next < events.size() && events[next].level < height[cell]; ++next) {
            sweep.overCol(events[next]);
        }
        if (ocean[cell] == kLand) {
            sweep.flood(cell);
        }
    }
    for (; next < events.size(); ++next) {
        sweep.overCol(events[next]);
    }
    // The union-find becomes the labels: in the total order, every receiver comes first.
    result.label = sweep.release();
    for (const std::uint32_t cell : order) {
        std::int32_t& label = result.label[cell];
        if (ocean[cell] != kLand) {
            label = kOceanNode;
        } else if (receiver[cell] == kNoNeighbor) {
            label = result.own[cell]; // a pit started its leaf
        } else {
            label = result.label[grid.neighbor(cell, receiver[cell])];
        }
    }
    for (std::size_t n = 1; n < result.nodes.size(); ++n) {
        DepressionNode& node = result.nodes[n];
        if (node.parent < 0) {
            throw std::logic_error("buildHierarchy: a depression never reached the ocean");
        }
        node.geolink = result.label[node.partner];
    }
    return result;
}

} // namespace terrain::planet

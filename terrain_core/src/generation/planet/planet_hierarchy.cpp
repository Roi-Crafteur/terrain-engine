#include "terrain/generation/planet/planet_hierarchy.hpp"

#include "terrain/generation/planet/planet_relief.hpp"

#include <cstddef>
#include <stdexcept>
#include <utility>

namespace terrain::planet {

namespace {

// Union-find over the land cells (by slot) and one more slot, the whole ocean: a root holds
// -(node + 1), any other slot its parent slot.
class FloodSets {
public:
    explicit FloodSets(std::size_t slots) : parent_(slots, 0) {}

    std::int32_t root(std::int32_t slot) {
        while (parent_[static_cast<std::size_t>(slot)] >= 0) {
            const std::int32_t up = parent_[static_cast<std::size_t>(slot)];
            const std::int32_t grand = parent_[static_cast<std::size_t>(up)];
            if (grand >= 0) {
                parent_[static_cast<std::size_t>(slot)] = grand; // path halving
            }
            slot = parent_[static_cast<std::size_t>(slot)];
        }
        return slot;
    }
    std::int32_t node(std::int32_t root) const {
        return -parent_[static_cast<std::size_t>(root)] - 1;
    }
    void makeRoot(std::size_t slot, std::int32_t node) { parent_[slot] = -(node + 1); }
    void link(std::size_t slot, std::int32_t parent) { parent_[slot] = parent; }
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

class Sweep {
public:
    Sweep(const PlanetGrid& grid, const std::vector<float>& height, const LandIndex& land,
          DepressionHierarchy& result)
        : grid_(grid), height_(height), land_(land), result_(result),
          sets_(land.landCells() + 1), oceanRoot_(static_cast<std::int32_t>(land.landCells())) {}

    void joinOcean();
    void flood(std::size_t cell);
    std::vector<std::int32_t> release() { return sets_.release(); }

private:
    [[nodiscard]] std::int32_t slotOf(std::size_t cell) const {
        return static_cast<std::int32_t>(land_.slot(cell));
    }
    Around floodedAround(std::size_t cell);
    void mergeAll(std::size_t cell, const Around& around);
    void attachToOcean(std::size_t cell, const Around& around, int oceanSlot);

    const PlanetGrid& grid_;
    const std::vector<float>& height_;
    const LandIndex& land_;
    DepressionHierarchy& result_;
    FloodSets sets_;
    std::int32_t oceanRoot_; // the slot of the whole ocean, after the land
};

void Sweep::joinOcean() {
    result_.nodes.assign(1, DepressionNode{}); // the ocean
    sets_.makeRoot(static_cast<std::size_t>(oceanRoot_), kOceanNode);
}

Around Sweep::floodedAround(std::size_t cell) {
    Around around;
    for (int k = 0; k < 8; ++k) {
        const std::size_t n = grid_.neighbor(cell, k);
        const bool land = land_.land(n);
        if (land && !lowerCell(height_, n, cell)) {
            continue; // not flooded yet (the ocean always is, being lower)
        }
        const std::int32_t root = land ? sets_.root(slotOf(n)) : oceanRoot_;
        int slot = 0;
        while (slot < around.count && around.roots[static_cast<std::size_t>(slot)] != root) {
            ++slot;
        }
        const auto s = static_cast<std::size_t>(slot);
        if (slot == around.count) {
            around.roots[s] = root;
            around.lowest[s] = static_cast<std::uint32_t>(n);
            ++around.count;
        } else if (lowerCell(height_, n, around.lowest[s])) {
            around.lowest[s] = static_cast<std::uint32_t>(n);
        }
    }
    return around;
}

void Sweep::flood(std::size_t cell) {
    const Around around = floodedAround(cell);
    const auto slot = static_cast<std::size_t>(slotOf(cell));
    if (around.count == 0) {
        const auto leaf = static_cast<std::int32_t>(result_.nodes.size());
        DepressionNode node;
        node.pit = static_cast<std::uint32_t>(cell);
        result_.nodes.push_back(node);
        sets_.makeRoot(slot, leaf);
        result_.own.add(cell, leaf);
        return;
    }
    if (around.count == 1) {
        sets_.link(slot, around.roots[0]);
        const std::int32_t node = sets_.node(around.roots[0]);
        if (node != kOceanNode) {
            result_.own.add(cell, node);
        }
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
        sets_.link(static_cast<std::size_t>(root), oceanRoot_);
    }
    sets_.link(static_cast<std::size_t>(slotOf(cell)), oceanRoot_); // own: the ocean
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
        }
        sets_.link(static_cast<std::size_t>(root), keep);
        sets_.makeRoot(static_cast<std::size_t>(keep), merged);
        current = merged;
        if (lowerCell(height_, otherLowest, currentLowest)) {
            currentLowest = otherLowest;
        }
    }
    sets_.link(static_cast<std::size_t>(slotOf(cell)), keep);
    result_.own.add(cell, current);
}

} // namespace

DepressionHierarchy buildHierarchy(const PlanetGrid& grid, const std::vector<float>& height,
                                   const std::shared_ptr<const LandIndex>& land,
                                   const std::vector<std::uint32_t>& order,
                                   const LandValues<std::uint8_t>& receiver) {
    DepressionHierarchy result;
    result.own = SparseCells<std::int32_t>(grid.cells(), kOceanNode);
    Sweep sweep(grid, height, *land, result);
    sweep.joinOcean();
    for (const std::uint32_t cell : order) {
        if (land->land(cell)) {
            sweep.flood(cell);
        }
    }
    result.own.finish();
    // The union-find becomes the labels: in the total order, every receiver comes first.
    std::vector<std::int32_t> labels = sweep.release();
    labels.resize(land->landCells()); // without the ocean
    for (const std::uint32_t cell : order) {
        if (!land->land(cell)) {
            continue;
        }
        const std::uint8_t to = receiver[cell];
        std::int32_t& label = labels[land->slot(cell)];
        if (to == kNoNeighbor) {
            label = result.own[cell]; // a pit started its leaf
        } else {
            const std::size_t next = grid.neighbor(cell, to);
            label = land->land(next) ? labels[land->slot(next)] : kOceanNode;
        }
    }
    result.label = LandValues<std::int32_t>(land, std::move(labels), kOceanNode);
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

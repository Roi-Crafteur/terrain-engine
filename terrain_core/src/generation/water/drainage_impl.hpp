#pragma once

// The inside of the fine network (drainage_network.hpp), internal to terrain_core.

#include "network_block.hpp"
#include "network_cache.hpp"
#include "terrain/generation/water/drainage_network.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>

namespace terrain {

using water::CellNodes;
using water::NetworkBlock;
using water::NetworkNode;
using water::NodeOutlet;

struct DrainageNetwork::Impl {
    Impl(std::uint64_t seed, const Heightfield& r, std::shared_ptr<const PlanetLevel> p,
         const DrainageNetworkParams& pr)
        : relief(r), planet(std::move(p)), network(*planet), ground(seed, r.world()),
          lattice(water::nodeLattice(planet->grid(), seed)), params(pr), cache(pr.cacheBytes) {}

    Heightfield relief;
    std::shared_ptr<const PlanetLevel> planet;
    PlanetNetwork network;
    Permeability ground;
    water::NodeLattice lattice;
    DrainageNetworkParams params;
    mutable water::NetworkCache cache;

    [[nodiscard]] std::shared_ptr<const CellNodes> nodesOf(int ci, int cj) const {
        return cache.nodes(planet->grid().index(ci, cj), [&] {
            return water::cellNodes(relief, ground, lattice, ci, cj);
        });
    }
    [[nodiscard]] std::shared_ptr<const NetworkBlock> blockOf(int ci, int cj) const {
        const PlanetGrid& g = planet->grid();
        const int i = g.wrapColumn(ci);
        const int j = g.wrapRow(cj);
        return cache.block(g.index(i, j), [&] {
            return water::computeBlock(
                network, lattice, [this](int a, int b) { return nodesOf(a, b); }, i, j,
                params.radius);
        });
    }
    // The node of global indices (gi, gj) and its block.
    struct Found {
        std::shared_ptr<const NetworkBlock> block;
        const NetworkNode* node = nullptr;
    };
    [[nodiscard]] Found node(std::int64_t gi, std::int64_t gj) const {
        const std::int64_t i = lattice.wrapI(gi);
        const std::int64_t j = lattice.wrapJ(gj);
        const int n = lattice.perCell;
        Found f;
        f.block = blockOf(static_cast<int>(i / n), static_cast<int>(j / n));
        f.node = &f.block->nodes[static_cast<std::size_t>((j % n) * n + (i % n))];
        return f;
    }
    // The position of node (gi, gj) as the image nearest to (x, z).
    [[nodiscard]] std::array<double, 2> near(std::int64_t gi, std::int64_t gj, double x,
                                             double z) const {
        const std::array<double, 2> p = lattice.position(gi, gj);
        return {nearestImage(p[0], x, relief.world().x), nearestImage(p[1], z, relief.world().z)};
    }
    [[nodiscard]] std::int64_t column(double x) const {
        return static_cast<std::int64_t>(
            std::floor((wrapCoordinate(x, relief.world().x) + 0.5 * relief.world().x) /
                       water::kNodeSpacing));
    }
    [[nodiscard]] std::int64_t row(double z) const {
        return static_cast<std::int64_t>(
            std::floor((wrapCoordinate(z, relief.world().z) + 0.5 * relief.world().z) /
                       water::kNodeSpacing));
    }
    [[nodiscard]] float dischargeOf(const NetworkNode& n) const {
        return n.outlet == NodeOutlet::Trunk ? static_cast<float>(network.discharge(n.trunkCell))
                                             : n.discharge;
    }
    [[nodiscard]] std::uint8_t orderOf(const NetworkNode& n) const {
        return n.outlet == NodeOutlet::Trunk ? network.order(n.trunkCell) : n.order;
    }
    // The basin of the water of node (gi, gj): down to the trunk, the sea or the lake it reaches.
    [[nodiscard]] std::uint32_t basinOf(std::int64_t gi, std::int64_t gj) const;
    // The stream of node (bi, bj), distance meters from the point (x, z), into point.
    void describe(std::int64_t bi, std::int64_t bj, double x, double z, double distance,
                  NetworkPoint& point) const;
};


} // namespace terrain

// The fine network of the streams (drainage_network.hpp, task 11C step 1) on the small world:
// every node drains to an outlet, the water never rises downstream, the discharge, the width and the
// order never fall downstream, the same bits after the cache drops its blocks and with the blocks
// asked in another order, a wider window changes little, more channels where it is wet and steep,
// and no grid in the directions of the streams.
#include "terrain/generation/water/drainage_network.hpp"

#include "../terrain_core/src/generation/water/network_block.hpp"
#include "network_flags_support.hpp"
#include "planet_golden_support.hpp"

#include "test_assert.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

struct World {
    terrain::Heightfield relief = terrain::Heightfield(terrain::kDefaultWorldSeed,
                                                       terrain::HeightfieldParams{},
                                                       golden::kSmallPlanet)
                                      .withoutCoasts();
    std::shared_ptr<const terrain::PlanetLevel> planet = [this] {
        const terrain::PlanetParams params =
            network_flags::flagged(golden::smallPlanetParams(2, 2), network_flags::kStepOne);
        return std::make_shared<const terrain::PlanetLevel>(
            terrain::planet::buildPlanet(relief, params), params);
    }();
};

const World& world() {
    static const World w;
    return w;
}

// A land cell with a trunk under the mountains, and a dry cell of a plain.
std::size_t findCell(const terrain::PlanetNetwork& net, bool wetMountain) {
    const terrain::PlanetLevel& level = net.level();
    std::size_t best = 0;
    double score = -1.0;
    for (std::size_t c = 0; c < level.grid().cells(); ++c) {
        if (!net.land(c) || (level.cells().bits[c] & terrain::PlanetCells::kLakeBit) != 0) {
            continue;
        }
        const double rain = level.precipitation(c);
        const double s = wetMountain ? (net.trunk(c) ? rain * level.elevation(c) : 0.0)
                                     : (level.elevation(c) < 200.0f ? 1.0 / (1.0 + rain) : 0.0);
        if (s > score) {
            score = s;
            best = c;
        }
    }
    return best;
}

std::vector<terrain::StreamPiece> piecesOfCell(const terrain::DrainageNetwork& net, std::size_t c) {
    const terrain::PlanetGrid& g = net.planetNetwork().level().grid();
    const double x = g.centerX(g.column(c));
    const double z = g.centerZ(g.row(c));
    const double h = 0.5 * g.cellSize;
    return net.piecesIn(x - h, z - h, x + h - 1.0, z + h - 1.0);
}

bool samePieces(const std::vector<terrain::StreamPiece>& a,
                const std::vector<terrain::StreamPiece>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t k = 0; k < a.size(); ++k) {
        if (a[k].x0 != b[k].x0 || a[k].z1 != b[k].z1 || a[k].discharge != b[k].discharge ||
            a[k].order != b[k].order || a[k].wet != b[k].wet) {
            return false;
        }
    }
    return true;
}

void testDownstream(const terrain::DrainageNetwork& net, std::size_t cell) {
    const std::vector<terrain::StreamPiece> pieces = piecesOfCell(net, cell);
    std::size_t channels = 0;
    std::size_t lattice = 0;
    for (const terrain::StreamPiece& p : pieces) {
        channels += p.trunk ? 0 : 1;
        const double angle = std::atan2(p.z1 - p.z0, p.x1 - p.x0) * 57.29577951308232;
        const double off = std::fmod(std::abs(angle) + 360.0, 45.0);
        lattice += (off < 2.0 || off > 43.0) ? 1 : 0;
    }
    std::printf("cell %zu: %zu pieces, %zu of channels, %.0f %% along the 8 directions of the "
                "lattice\n",
                cell, pieces.size(), channels,
                pieces.empty() ? 0.0 : 100.0 * static_cast<double>(lattice) /
                                           static_cast<double>(pieces.size()));
    TEST_CHECK(channels > 0);
    TEST_CHECK(lattice * 3 < pieces.size()); // no grid in the directions
}

// The block of a cell computed directly: every node drains (an outlet or a step), the water never
// rises downstream, the discharge and the order never fall along the channels; and the share of
// the nodes whose way out is the same with a window of 5 x 5 cells.
void testBlock(const terrain::DrainageNetwork& net, const World& w, std::size_t cell) {
    using namespace terrain::water;
    const terrain::PlanetGrid& g = w.planet->grid();
    const NodeLattice lattice = nodeLattice(g, terrain::kDefaultWorldSeed);
    const terrain::Permeability ground(terrain::kDefaultWorldSeed, w.relief.world());
    const CellNodesOf nodesOf = [&](int ci, int cj) {
        return std::make_shared<const CellNodes>(cellNodes(w.relief, ground, lattice, ci, cj));
    };
    const int ci = g.column(cell);
    const int cj = g.row(cell);
    const NetworkBlock b = computeBlock(net.planetNetwork(), lattice, nodesOf, ci, cj, 1);
    const NetworkBlock wide = computeBlock(net.planetNetwork(), lattice, nodesOf, ci, cj, 2);
    const int n = lattice.perCell;
    std::size_t lost = 0;
    std::size_t rises = 0;
    std::size_t falls = 0;
    std::size_t same = 0;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const NetworkNode& a = b.nodes[static_cast<std::size_t>(y * n + x)];
            const NetworkNode& c = wide.nodes[static_cast<std::size_t>(y * n + x)];
            same += a.downX == c.downX && a.downZ == c.downZ ? 1 : 0;
            if (a.downX == kNoStep) {
                lost += a.outlet == NodeOutlet::None ? 1 : 0;
                continue;
            }
            const int dx = x + a.downX;
            const int dz = y + a.downZ;
            if (dx < 0 || dz < 0 || dx >= n || dz >= n || a.outlet != NodeOutlet::None) {
                continue;
            }
            const NetworkNode& d = b.nodes[static_cast<std::size_t>(dz * n + dx)];
            rises += d.filled > a.filled ? 1 : 0;
            if (a.channel && d.outlet == NodeOutlet::None) {
                falls += d.discharge < a.discharge || d.order < a.order || !d.channel ? 1 : 0;
            }
        }
    }
    std::printf("block: %zu nodes without a way, %zu rising, %zu channels falling; %.1f %% of the "
                "ways the same with a window of 5 x 5 cells\n",
                lost, rises, falls, 100.0 * static_cast<double>(same) / (n * n));
    TEST_CHECK(lost == 0 && rises == 0 && falls == 0);
    TEST_CHECK(same * 100 >= static_cast<std::size_t>(95 * n * n));
}

} // namespace

int main() {
    const World& w = world();
    const terrain::DrainageNetwork net(terrain::kDefaultWorldSeed, w.relief, w.planet);
    const std::size_t wet = findCell(net.planetNetwork(), true);
    const std::size_t dry = findCell(net.planetNetwork(), false);
    const auto start = std::chrono::steady_clock::now();
    const std::vector<terrain::StreamPiece> first = piecesOfCell(net, wet);
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("the first cell in %.2f s, %zu blocks, cache %.1f MB\n", seconds,
                net.blocksComputed(), static_cast<double>(net.cacheBytes()) * 1e-6);
    testDownstream(net, wet);
    testBlock(net, w, wet);
    // Fewer channels on a dry plain than on a wet mountain.
    std::size_t dryChannels = 0;
    for (const terrain::StreamPiece& p : piecesOfCell(net, dry)) {
        dryChannels += p.trunk ? 0 : 1;
    }
    std::size_t wetChannels = 0;
    for (const terrain::StreamPiece& p : first) {
        wetChannels += p.trunk ? 0 : 1;
    }
    std::printf("channels: %zu pieces in a wet mountain cell, %zu in a dry plain cell\n",
                wetChannels, dryChannels);
    TEST_CHECK(wetChannels > 2 * dryChannels);
    // The same bits with a cache too small to keep anything and blocks asked in another order.
    terrain::DrainageNetworkParams tiny;
    tiny.cacheBytes = 3'000'000; // the blocks of about one cell: evicted between the cells
    const terrain::DrainageNetwork small(terrain::kDefaultWorldSeed, w.relief, w.planet, tiny);
    (void)piecesOfCell(small, dry);
    TEST_CHECK(samePieces(piecesOfCell(small, wet), first));
    TEST_CHECK(small.cacheBytes() <= 3'000'000 + 400'000);
    return test::result();
}

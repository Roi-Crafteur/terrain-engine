#include "network_lattice.hpp"

#include "terrain/generation/geography.hpp"
#include "terrain/generation/noise.hpp"

#include <cmath>
#include <stdexcept>

namespace terrain::water {

namespace {

constexpr std::uint64_t kJitterStream = 0x11C1;

double jitterOf(std::uint64_t seed, std::int64_t i, std::int64_t j, std::uint64_t axis) {
    const auto key = static_cast<std::uint64_t>(i) * 0x9E3779B97F4A7C15ULL ^
                     (static_cast<std::uint64_t>(j) + axis * 0x632BE59BD9B4E019ULL);
    const std::uint64_t bits = noise::deriveSeed(seed, key) >> 11;
    return (2.0 * static_cast<double>(bits) * 0x1.0p-53 - 1.0) * kNodeJitter;
}

} // namespace

std::array<double, 2> NodeLattice::position(std::int64_t i, std::int64_t j) const {
    const std::int64_t wi = wrapI(i);
    const std::int64_t wj = wrapJ(j);
    const double x = (static_cast<double>(wi) + 0.5 + jitterOf(seed, wi, wj, 0)) * kNodeSpacing -
                     0.5 * static_cast<double>(nx) * kNodeSpacing;
    const double z = (static_cast<double>(wj) + 0.5 + jitterOf(seed, wi, wj, 1)) * kNodeSpacing -
                     0.5 * static_cast<double>(nz) * kNodeSpacing;
    return {x, z};
}

NodeLattice nodeLattice(const PlanetGrid& grid, std::uint64_t seed) {
    const double perCell = grid.cellSize / kNodeSpacing;
    if (perCell != std::floor(perCell) || perCell < 1.0) {
        throw std::invalid_argument("nodeLattice: the nodes must divide the cells of the planet");
    }
    NodeLattice lattice;
    lattice.perCell = static_cast<int>(perCell);
    lattice.nx = static_cast<std::int64_t>(grid.nx) * lattice.perCell;
    lattice.nz = static_cast<std::int64_t>(grid.nz) * lattice.perCell;
    lattice.seed = noise::deriveSeed(seed, kJitterStream);
    return lattice;
}

CellNodes cellNodes(const Heightfield& relief, const Permeability& ground,
                    const NodeLattice& lattice, int ci, int cj) {
    const int n = lattice.perCell;
    const auto count = static_cast<std::size_t>(n) * static_cast<std::size_t>(n);
    std::vector<double> xs(count);
    std::vector<double> zs(count);
    for (int b = 0; b < n; ++b) {
        for (int a = 0; a < n; ++a) {
            const std::array<double, 2> p = lattice.position(
                static_cast<std::int64_t>(ci) * n + a, static_cast<std::int64_t>(cj) * n + b);
            xs[static_cast<std::size_t>(b * n + a)] = p[0];
            zs[static_cast<std::size_t>(b * n + a)] = p[1];
        }
    }
    std::vector<HeightSample> samples(count);
    relief.samplesAt(xs, zs, samples);
    CellNodes nodes;
    nodes.height.resize(count);
    nodes.sea.resize(count);
    nodes.permeability.resize(count);
    const Geography* geography = relief.geography();
    for (std::size_t k = 0; k < count; ++k) {
        nodes.height[k] = samples[k].height;
        nodes.sea[k] = samples[k].water == WaterKind::Ocean ? 1 : 0;
        const float character =
            geography != nullptr ? geography->sampleAt(xs[k], zs[k]).character : 0.5f;
        nodes.permeability[k] = ground.at(xs[k], zs[k], samples[k], character);
    }
    return nodes;
}

} // namespace terrain::water

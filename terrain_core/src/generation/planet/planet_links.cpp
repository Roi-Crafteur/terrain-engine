#include "terrain/generation/planet/planet_links.hpp"

#include "planet_parallel.hpp"
#include "terrain/generation/planet/planet_relief.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace terrain::planet {

float linkBarrier(const Heightfield& relief, double x0, double z0, double x1, double z1,
                  const LinkLadder& params) {
    return linkBarrier(
        [&relief](std::span<const double> xs, std::span<const double> zs, std::span<float> out) {
            relief.heightsAt(xs, zs, out);
        },
        x0, z0, x1, z1, params);
}

float linkBarrier(const LinkHeights& relief, double x0, double z0, double x1, double z1,
                  const LinkLadder& params) {
    if (params.sections < 1 || params.lanes < 1 || params.lanes % 2 == 0) {
        throw std::invalid_argument("linkBarrier: sections >= 1 and an odd count of lanes");
    }
    const double dx = x1 - x0;
    const double dz = z1 - z0;
    const double length = std::sqrt(dx * dx + dz * dz);
    const double px = length > 0.0 ? -dz / length : 0.0;
    const double pz = length > 0.0 ? dx / length : 0.0;
    const auto sections = static_cast<std::size_t>(params.sections);
    const auto lanes = static_cast<std::size_t>(params.lanes);
    const auto middle = static_cast<double>(params.lanes / 2);
    std::vector<double> xs(sections * lanes);
    std::vector<double> zs(sections * lanes);
    for (std::size_t s = 0; s < sections; ++s) {
        const double t = (static_cast<double>(s) + 0.5) / static_cast<double>(sections);
        for (std::size_t l = 0; l < lanes; ++l) {
            const double offset = (static_cast<double>(l) - middle) * params.laneSpacing;
            xs[s * lanes + l] = x0 + dx * t + px * offset;
            zs[s * lanes + l] = z0 + dz * t + pz * offset;
        }
    }
    std::vector<float> heights(xs.size());
    relief(xs, zs, heights);
    // The lowest of the highest points of the ways: each lane of a section reached from the
    // lane beside it or the same one in the section before.
    const auto center = static_cast<std::size_t>(params.lanes / 2);
    const float far = std::numeric_limits<float>::infinity();
    std::vector<float> best(lanes, far);
    std::vector<float> next(lanes, far);
    for (std::size_t l = 0; l < lanes; ++l) {
        best[l] = (l + 1 >= center && l <= center + 1) ? heights[l] : far;
    }
    for (std::size_t s = 1; s < sections; ++s) {
        for (std::size_t l = 0; l < lanes; ++l) {
            float lowest = best[l];
            if (l > 0) {
                lowest = std::min(lowest, best[l - 1]);
            }
            if (l + 1 < lanes) {
                lowest = std::min(lowest, best[l + 1]);
            }
            next[l] = std::max(lowest, heights[s * lanes + l]);
        }
        std::swap(best, next);
    }
    float barrier = far;
    for (std::size_t l = 0; l < lanes; ++l) {
        if (l + 1 >= center && l <= center + 1) {
            barrier = std::min(barrier, best[l]);
        }
    }
    return barrier;
}

namespace {

// The neighbor opposite to neighbor k (kPlanetNeighbors).
int oppositeOf(int k) {
    const PlanetNeighbor& n = kPlanetNeighbors[static_cast<std::size_t>(k)];
    for (int o = 0; o < 8; ++o) {
        const PlanetNeighbor& m = kPlanetNeighbors[static_cast<std::size_t>(o)];
        if (m.dx == -n.dx && m.dz == -n.dz) {
            return o;
        }
    }
    return k;
}

const CellExits* exitsOf(const PlanetGrid& grid, const std::vector<std::vector<CellExits>>& rows,
                         std::size_t cell) {
    const std::vector<CellExits>& row = rows[static_cast<std::size_t>(grid.row(cell))];
    const int column = grid.column(cell);
    const auto it = std::lower_bound(
        row.begin(), row.end(), column,
        [](const CellExits& e, int c) { return static_cast<int>(e.column) < c; });
    return it != row.end() && it->column == column ? &*it : nullptr;
}

// The closed links of one cell with exits.
std::uint8_t closedOf(const PlanetGrid& grid, const std::vector<std::vector<CellExits>>& rows,
                      std::size_t cell, const CellExits& from, double tolerance) {
    std::uint8_t mask = 0;
    for (int k = 0; k < 8; ++k) {
        double barrier = from.exit(static_cast<std::size_t>(k));
        if (const CellExits* to = exitsOf(grid, rows, grid.neighbor(cell, k)); to != nullptr) {
            barrier = std::max(barrier, to->exit(static_cast<std::size_t>(oppositeOf(k))));
        }
        if (barrier - static_cast<double>(from.floor) > tolerance) {
            mask = static_cast<std::uint8_t>(mask | (1u << k));
        }
    }
    return mask;
}

} // namespace

LandValues<std::uint8_t> closedLinks(const PlanetGrid& grid,
                                     const std::vector<std::vector<CellExits>>& rows,
                                     const std::shared_ptr<const LandIndex>& land,
                                     const PlanetLinkParams& params, int threads) {
    LandValues<std::uint8_t> closed(land, 0, 0);
    if (rows.empty()) {
        return closed;
    }
    std::vector<std::uint8_t>& values = closed.values();
    parallelRows(grid.nz, threads, [&](int first, int last) {
        for (int j = first; j < last; ++j) {
            for (const CellExits& e : rows[static_cast<std::size_t>(j)]) {
                const std::size_t cell = grid.index(e.column, j);
                if (land->land(cell)) {
                    values[land->slot(cell)] = closedOf(grid, rows, cell, e, params.tolerance);
                }
            }
        }
    });
    return closed;
}

} // namespace terrain::planet

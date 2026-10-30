#include "analysis_distance.hpp"

#include "../planet/planet_parallel.hpp"
#include "terrain/generation/settlement/settlement_maps.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace terrain::settlement {

namespace {

constexpr int kNone = kMaxDistanceCells + 1;
constexpr int kNoneSquared = kNone * kNone;
// Columns per block of the column pass.
constexpr int kColumnBlock = 64;

void columnPass(const PlanetGrid& grid, std::vector<std::uint8_t>& d, int i) {
    const auto at = [&](int j) -> std::uint8_t& {
        return d[grid.index(i, grid.wrapRow(j))];
    };
    for (int j = 0; j < grid.nz; ++j) {
        at(j) = at(j) == 0 ? 0 : static_cast<std::uint8_t>(kNone);
    }
    // Down then up, each over the period and kNone more cells, so that the seam passes.
    for (int j = 1; j < grid.nz + kNone; ++j) {
        const int next = std::min(kNone, at(j - 1) + 1);
        if (next < at(j)) {
            at(j) = static_cast<std::uint8_t>(next);
        }
    }
    for (int j = grid.nz + kNone - 2; j >= -kNone; --j) {
        const int next = std::min(kNone, at(j + 1) + 1);
        if (next < at(j)) {
            at(j) = static_cast<std::uint8_t>(next);
        }
    }
}

// The squared distance at (i, j) from the column distances of its row.
int squaredAt(const PlanetGrid& grid, const std::vector<std::uint8_t>& columns, int i, int j) {
    int best = kNoneSquared;
    const int reach = std::min(kMaxDistanceCells, grid.nx / 2);
    for (int di = -reach; di <= reach; ++di) {
        const int g = columns[grid.index(grid.wrapColumn(i + di), j)];
        if (g >= kNone) {
            continue;
        }
        best = std::min(best, g * g + di * di);
    }
    return best;
}

} // namespace

void columnDistances(const PlanetGrid& grid, std::vector<std::uint8_t>& columns, int threads) {
    planet::parallelBlocks(grid.nx, kColumnBlock, planet::resolveThreads(threads),
                           [&](int begin, int end) {
                               for (int i = begin; i < end; ++i) {
                                   columnPass(grid, columns, i);
                               }
                           });
}

planet::LandValues<std::uint8_t>
landDistances(const PlanetGrid& grid, const std::shared_ptr<const planet::LandIndex>& land,
              const std::vector<std::uint8_t>& columns, int threads) {
    planet::LandValues<std::uint8_t> km(land, kFarDistance, kFarDistance);
    const double cellKm = grid.cellSize / 1000.0;
    planet::parallelRows(grid.nz, planet::resolveThreads(threads), [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < grid.nx; ++i) {
                const std::size_t cell = grid.index(i, j);
                if (!land->land(cell)) {
                    continue;
                }
                const int d2 = squaredAt(grid, columns, i, j);
                if (d2 > kMaxDistanceCells * kMaxDistanceCells) {
                    continue;
                }
                const double d = std::sqrt(static_cast<double>(d2)) * cellKm;
                km.at(cell) = static_cast<std::uint8_t>(std::min(254.0, std::floor(d + 0.5)));
            }
        }
    });
    return km;
}

std::vector<std::uint16_t> squaredDistances(const PlanetGrid& grid,
                                            const std::vector<std::uint8_t>& columns,
                                            int threads) {
    std::vector<std::uint16_t> out(grid.cells(), static_cast<std::uint16_t>(kNoneSquared));
    planet::parallelRows(grid.nz, planet::resolveThreads(threads), [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < grid.nx; ++i) {
                out[grid.index(i, j)] = static_cast<std::uint16_t>(squaredAt(grid, columns, i, j));
            }
        }
    });
    return out;
}

} // namespace terrain::settlement

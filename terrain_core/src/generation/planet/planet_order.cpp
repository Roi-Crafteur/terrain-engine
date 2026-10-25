#include "terrain/generation/planet/planet_order.hpp"

#include "planet_parallel.hpp"
#include "terrain/generation/planet/planet_relief.hpp"

#include <algorithm>
#include <cstddef>

namespace terrain::planet {

namespace {

constexpr std::size_t kMinBlock = 1u << 16; // cells per sorted block, at least

} // namespace

std::vector<std::uint32_t> sortCells(const std::vector<float>& height, int threads) {
    const std::size_t count = height.size();
    std::vector<std::uint32_t> order(count);
    for (std::size_t c = 0; c < count; ++c) {
        order[c] = static_cast<std::uint32_t>(c);
    }
    const auto less = [&height](std::uint32_t a, std::uint32_t b) {
        return lowerCell(height, a, b);
    };
    const int workers = resolveThreads(threads);
    const std::size_t blocks = std::max<std::size_t>(
        1, std::min<std::size_t>(static_cast<std::size_t>(workers), count / kMinBlock));
    std::vector<std::size_t> bounds(blocks + 1);
    for (std::size_t b = 0; b <= blocks; ++b) {
        bounds[b] = count * b / blocks;
    }
    const auto begin = [&](std::size_t b) {
        return order.begin() + static_cast<std::ptrdiff_t>(bounds[b]);
    };
    parallelBlocks(static_cast<int>(blocks), 1, workers, [&](int first, int last) {
        for (int b = first; b < last; ++b) {
            std::sort(begin(static_cast<std::size_t>(b)), begin(static_cast<std::size_t>(b) + 1),
                      less);
        }
    });
    // Merge neighboring runs until one is left.
    for (std::size_t width = 1; width < blocks; width *= 2) {
        const auto pairs = static_cast<int>((blocks + 2 * width - 1) / (2 * width));
        parallelBlocks(pairs, 1, workers, [&](int first, int last) {
            for (int p = first; p < last; ++p) {
                const std::size_t lo = static_cast<std::size_t>(p) * 2 * width;
                const std::size_t mid = std::min(lo + width, blocks);
                const std::size_t hi = std::min(lo + 2 * width, blocks);
                std::inplace_merge(begin(lo), begin(mid), begin(hi), less);
            }
        });
    }
    return order;
}

std::vector<std::uint8_t> computeReceivers(const PlanetGrid& grid, const std::vector<float>& height,
                                           const std::vector<std::uint8_t>& ocean, int threads,
                                           const PlanetCols* cols) {
    std::vector<std::uint8_t> receiver(grid.cells(), kNoNeighbor);
    parallelRows(grid.nz, resolveThreads(threads), [&](int first, int last) {
        for (std::size_t c = grid.index(0, first); c < grid.index(0, last); ++c) {
            if (ocean[c] != kLand) {
                continue;
            }
            double best = -1.0;
            for (int k = 0; k < 8; ++k) {
                const std::size_t n = grid.neighbor(c, k);
                if (!lowerCell(height, n, c) || (cols != nullptr && cols->held(height, c, k))) {
                    continue;
                }
                const double drop = static_cast<double>(height[c]) - height[n];
                const double slope = kPlanetNeighbors[static_cast<std::size_t>(k)].diagonal
                                         ? drop / kPlanetDiagonal
                                         : drop;
                if (slope > best) {
                    best = slope;
                    receiver[c] = static_cast<std::uint8_t>(k);
                }
            }
        }
    });
    return receiver;
}

} // namespace terrain::planet

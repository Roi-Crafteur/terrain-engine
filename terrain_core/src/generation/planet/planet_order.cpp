#include "terrain/generation/planet/planet_order.hpp"

#include "planet_parallel.hpp"
#include "terrain/generation/planet/planet_relief.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace terrain::planet {

namespace {

constexpr std::size_t kMinBlock = 1u << 16; // cells per sorted block, at least

// Sorts cells in the total order: in blocks on up to threads threads, then merged.
void sortInOrder(std::vector<std::uint32_t>& order, const std::vector<float>& height, int threads) {
    const std::size_t count = order.size();
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
}

} // namespace

std::vector<std::uint32_t> sortCells(const std::vector<float>& height, int threads) {
    std::vector<std::uint32_t> order(height.size());
    for (std::size_t c = 0; c < order.size(); ++c) {
        order[c] = static_cast<std::uint32_t>(c);
    }
    sortInOrder(order, height, threads);
    return order;
}

std::vector<std::uint32_t> sortLandCells(const std::vector<float>& height, const LandIndex& land,
                                         int threads) {
    std::vector<std::uint32_t> order(land.landCells());
    land.forEachLand([&order](std::size_t cell, std::size_t slot) {
        order[slot] = static_cast<std::uint32_t>(cell);
    });
    sortInOrder(order, height, threads);
    return order;
}

namespace {

// The neighbors of a land cell below it, steepest first (drop / distance, sqrt(2) on the
// diagonals; the first in the order of kPlanetNeighbors on a tie).
struct Lower {
    double slope = 0.0;
    std::uint8_t to = kNoNeighbor;
};

int lowerNeighbors(const PlanetGrid& grid, const std::vector<float>& height, std::size_t c,
                   std::array<Lower, 8>& lower) {
    int count = 0;
    for (int k = 0; k < 8; ++k) {
        const std::size_t n = grid.neighbor(c, k);
        if (!lowerCell(height, n, c)) {
            continue;
        }
        const double drop = static_cast<double>(height[c]) - height[n];
        const double slope =
            kPlanetNeighbors[static_cast<std::size_t>(k)].diagonal ? drop / kPlanetDiagonal : drop;
        lower[static_cast<std::size_t>(count++)] = {slope, static_cast<std::uint8_t>(k)};
    }
    std::stable_sort(lower.begin(), lower.begin() + count,
                     [](const Lower& a, const Lower& b) { return a.slope > b.slope; });
    return count;
}

// The steepest neighbor below the cell, or with links the steepest whose way from the cell
// crosses no crest (planet_links.hpp), the steepest one when none does.
std::uint8_t receiverOf(const PlanetGrid& grid, const std::vector<float>& height, std::size_t c,
                        const ReceiverLinks* links) {
    std::array<Lower, 8> lower{};
    const int count = lowerNeighbors(grid, height, c, lower);
    if (count == 0) {
        return kNoNeighbor;
    }
    const std::uint8_t closed = links == nullptr ? 0 : links->closed(c);
    if (closed == 0) {
        return lower[0].to;
    }
    for (int n = 0; n < count; ++n) {
        const std::uint8_t k = lower[static_cast<std::size_t>(n)].to;
        if ((closed & (1u << k)) == 0) {
            return k;
        }
    }
    return lower[0].to;
}

} // namespace

LandValues<std::uint8_t> computeReceivers(const PlanetGrid& grid, const std::vector<float>& height,
                                          const std::shared_ptr<const LandIndex>& land,
                                          int threads, const ReceiverLinks* links) {
    LandValues<std::uint8_t> receiver(land, kNoNeighbor, kNoNeighbor);
    std::vector<std::uint8_t>& values = receiver.values();
    parallelRows(grid.nz, resolveThreads(threads), [&](int first, int last) {
        for (std::size_t c = grid.index(0, first); c < grid.index(0, last); ++c) {
            if (land->land(c)) {
                values[land->slot(c)] = receiverOf(grid, height, c, links);
            }
        }
    });
    return receiver;
}

} // namespace terrain::planet

#include "terrain/generation/rivers.hpp"

#include "grid_neighbors.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>
#include <queue>
#include <utility>

namespace terrain {

namespace {

using grid::kNeighbors;

// Height added to each filled cell above the one it drains to: a few float steps at the
// highest altitudes, so filled flats still slope toward their outlet.
constexpr float kFillEpsilon = 0.01f;

bool isBorder(const HeightGrid& grid, int column, int row) {
    return column == 0 || row == 0 || column == grid.size - 1 || row == grid.size - 1;
}

bool isOcean(float height) {
    return height < kSeaLevel;
}

int columnOf(const HeightGrid& grid, int index) {
    return index % grid.size;
}

int rowOf(const HeightGrid& grid, int index) {
    return index / grid.size;
}

// Rho8 (Fairfield & Leymarie 1991): the distance to the diagonal neighbors is 2 - u, with u
// uniform in [0, 1) and drawn per cell, instead of sqrt(2). On average flow lines follow the
// true downslope direction instead of locking onto one of the 8 directions.
float rho8DiagonalDistance(std::uint64_t seed, std::size_t index) {
    const std::uint64_t bits = noise::deriveSeed(seed, index);
    return 2.0f - static_cast<float>(bits >> 40) / 16777216.0f; // top 24 bits / 2^24
}

// Steepest strictly lower neighbor, or kNoReceiver. Ties keep the first neighbor.
int steepestReceiver(const HeightGrid& grid, int column, int row, float diagonalDistance) {
    const float height = grid.at(column, row);
    int best = kNoReceiver;
    float bestSlope = 0.0f;
    for (const grid::Neighbor& n : kNeighbors) {
        const int c = column + n.dx;
        const int r = row + n.dz;
        if (c < 0 || r < 0 || c >= grid.size || r >= grid.size) {
            continue;
        }
        const float distance = n.dx != 0 && n.dz != 0 ? diagonalDistance : 1.0f;
        const float slope = (height - grid.at(c, r)) / distance;
        if (slope > bestSlope) {
            bestSlope = slope;
            best = static_cast<int>(grid.index(c, r));
        }
    }
    return best;
}

// Donor of a cell with the largest catchment, or kNoReceiver if nothing drains into it.
int mainDonor(const HeightGrid& grid, const RiverNetwork& network, int index) {
    const int column = columnOf(grid, index);
    const int row = rowOf(grid, index);
    int best = kNoReceiver;
    double bestCatchment = 0.0;
    for (const grid::Neighbor& n : kNeighbors) {
        const int c = column + n.dx;
        const int r = row + n.dz;
        if (c < 0 || r < 0 || c >= grid.size || r >= grid.size) {
            continue;
        }
        const auto donor = static_cast<int>(grid.index(c, r));
        const double catchment = network.catchment[static_cast<std::size_t>(donor)];
        if (network.receiverOf(donor) == index && catchment > bestCatchment) {
            bestCatchment = catchment;
            best = donor;
        }
    }
    return best;
}

float waterHeight(const HeightGrid& grid, int index) {
    return std::max(grid.heights[static_cast<std::size_t>(index)], kSeaLevel);
}

// Steepest descent slope of the water surface at a cell, with the true neighbor distances.
// Unlike the slope toward the (Rho8) receiver, it does not depend on the direction the river
// takes, so it is the same everywhere on a plane.
float steepestSlope(const HeightGrid& grid, int index) {
    const int column = columnOf(grid, index);
    const int row = rowOf(grid, index);
    const float top = waterHeight(grid, index);
    const auto cellSize = static_cast<float>(grid.metersPerPixel);
    float steepest = 0.0f;
    for (const grid::Neighbor& n : kNeighbors) {
        const int c = column + n.dx;
        const int r = row + n.dz;
        if (c < 0 || r < 0 || c >= grid.size || r >= grid.size) {
            continue;
        }
        const float drop = top - waterHeight(grid, static_cast<int>(grid.index(c, r)));
        steepest = std::max(steepest, drop / (n.distance * cellSize));
    }
    return steepest;
}

} // namespace

void fillDepressions(HeightGrid& grid) {
    using Entry = std::pair<float, int>; // height, cell index: a total order
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    std::vector<bool> visited(grid.heights.size(), false);
    for (int row = 0; row < grid.size; ++row) {
        for (int column = 0; column < grid.size; ++column) {
            const std::size_t i = grid.index(column, row);
            if (isOcean(grid.heights[i]) || isBorder(grid, column, row)) {
                visited[i] = true;
                open.emplace(grid.heights[i], static_cast<int>(i));
            }
        }
    }
    while (!open.empty()) {
        const auto [height, index] = open.top();
        open.pop();
        const float raised = std::max(height + kFillEpsilon, std::nextafter(height, 1.0e9f));
        for (const grid::Neighbor& n : kNeighbors) {
            const int c = columnOf(grid, index) + n.dx;
            const int r = rowOf(grid, index) + n.dz;
            if (c < 0 || r < 0 || c >= grid.size || r >= grid.size || visited[grid.index(c, r)]) {
                continue;
            }
            visited[grid.index(c, r)] = true;
            float& neighbor = grid.at(c, r);
            neighbor = std::max(neighbor, raised);
            open.emplace(neighbor, static_cast<int>(grid.index(c, r)));
        }
    }
}

RiverNetwork computeDrainage(const HeightGrid& grid, std::uint64_t seed,
                             const RiverParams& params) {
    const std::size_t cells = grid.heights.size();
    RiverNetwork network{grid.size, grid.metersPerPixel, std::vector<int>(cells, kNoReceiver),
                         std::vector<double>(cells, 0.0), std::vector<std::uint8_t>(cells, 0),
                         std::vector<int>(cells), {}};
    const double cellArea = grid.metersPerPixel * grid.metersPerPixel;
    for (int row = 0; row < grid.size; ++row) {
        for (int column = 0; column < grid.size; ++column) {
            const std::size_t i = grid.index(column, row);
            if (isOcean(grid.heights[i])) {
                continue;
            }
            network.catchment[i] = cellArea;
            if (!isBorder(grid, column, row)) {
                network.receiver[i] =
                    steepestReceiver(grid, column, row, rho8DiagonalDistance(seed, i));
            }
        }
    }
    // Highest first: receivers are strictly lower, so every cell precedes its receiver.
    std::iota(network.downstreamOrder.begin(), network.downstreamOrder.end(), 0);
    std::sort(network.downstreamOrder.begin(), network.downstreamOrder.end(), [&](int a, int b) {
        const float ha = grid.heights[static_cast<std::size_t>(a)];
        const float hb = grid.heights[static_cast<std::size_t>(b)];
        return ha != hb ? ha > hb : a < b;
    });
    for (const int index : network.downstreamOrder) {
        const int receiver = network.receiverOf(index);
        if (receiver != kNoReceiver) {
            network.catchment[static_cast<std::size_t>(receiver)] +=
                network.catchment[static_cast<std::size_t>(index)];
        }
    }
    for (std::size_t i = 0; i < cells; ++i) {
        network.river[i] =
            !isOcean(grid.heights[i]) && network.catchment[i] >= params.minCatchmentArea ? 1 : 0;
    }
    return network;
}

float bedDepth(double catchment, const RiverParams& params) {
    const double doublings = std::log2(std::max(catchment / params.minCatchmentArea, 1.0));
    const double depth = params.bedDepth + params.bedDepthPerDoubling * doublings;
    return static_cast<float>(std::min(depth, static_cast<double>(params.maxBedDepth)));
}

void carveRiverBeds(HeightGrid& grid, const RiverNetwork& network, const RiverParams& params) {
    // Outlets first: a receiver is final before its donors are carved.
    for (auto it = network.downstreamOrder.rbegin(); it != network.downstreamOrder.rend(); ++it) {
        const int index = *it;
        const int receiver = network.receiverOf(index);
        if (!network.isRiver(index) || receiver == kNoReceiver) {
            continue;
        }
        const auto i = static_cast<std::size_t>(index);
        const float floor = waterHeight(grid, receiver);
        const float carved = grid.heights[i] - bedDepth(network.catchment[i], params);
        grid.heights[i] = std::min(grid.heights[i], std::max(carved, floor));
    }
}

std::vector<Cascade> detectCascades(const HeightGrid& grid, const RiverNetwork& network,
                                    const RiverParams& params) {
    std::vector<Cascade> cascades;
    for (int index = 0; index < static_cast<int>(grid.heights.size()); ++index) {
        const int receiver = network.receiverOf(index);
        if (!network.isRiver(index) || receiver == kNoReceiver) {
            continue;
        }
        const int donor = mainDonor(grid, network, index);
        if (donor == kNoReceiver) {
            continue; // a source: no slope above it
        }
        const float below = steepestSlope(grid, index);
        const float above = steepestSlope(grid, donor);
        if (below >= params.cascadeMinSlope && below - above >= params.cascadeMinBreak) {
            cascades.push_back({columnOf(grid, index), rowOf(grid, index),
                                waterHeight(grid, index), waterHeight(grid, receiver), above,
                                below, network.catchment[static_cast<std::size_t>(index)]});
        }
    }
    return cascades;
}

RiverNetwork generateRivers(HeightGrid& grid, std::uint64_t seed, const RiverParams& params) {
    fillDepressions(grid);
    RiverNetwork network = computeDrainage(grid, seed, params);
    carveRiverBeds(grid, network, params);
    network.cascades = detectCascades(grid, network, params);
    return network;
}

} // namespace terrain

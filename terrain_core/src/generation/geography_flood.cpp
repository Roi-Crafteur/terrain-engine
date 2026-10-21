#include "geography_stages.hpp"

#include "terrain/det_math.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

namespace terrain::geography {

namespace {

// A node waiting in the flood, ordered by elevation then index: a strict total order, so the
// flood visits the nodes in one order whatever the queue does with equal keys.
struct Wave {
    float elevation;
    std::size_t node;

    friend bool operator>(const Wave& a, const Wave& b) {
        return a.elevation > b.elevation || (a.elevation == b.elevation && a.node > b.node);
    }
};

using Queue = std::priority_queue<Wave, std::vector<Wave>, std::greater<>>;

bool onCoast(const Grid& grid, const std::vector<std::uint8_t>& land, int i, int j) {
    return land[grid.index(i + 1, j)] == 0 || land[grid.index(i - 1, j)] == 0 ||
           land[grid.index(i, j + 1)] == 0 || land[grid.index(i, j - 1)] == 0;
}

// The land nodes on the coast start the flood at their floor; without any coast (a world
// without sea), the lowest land node does.
void seedFlood(const Grid& grid, const Crust& crust, const std::vector<float>& floor,
               std::vector<float>& elevation, Queue& queue) {
    for (int j = 0; j < grid.nz; ++j) {
        for (int i = 0; i < grid.nx; ++i) {
            const std::size_t k = grid.index(i, j);
            if (crust.land[k] != 0 && onCoast(grid, crust.land, i, j)) {
                elevation[k] = floor[k];
                queue.push({floor[k], k});
            }
        }
    }
    if (queue.empty()) {
        std::size_t lowest = 0;
        for (std::size_t k = 1; k < floor.size(); ++k) {
            lowest = floor[k] < floor[lowest] ? k : lowest;
        }
        elevation[lowest] = floor[lowest];
        queue.push({floor[lowest], lowest});
    }
}

} // namespace

std::vector<float> drainedLand(const GeographyParams& params, const Grid& grid,
                               const Crust& crust, const std::vector<float>& floor) {
    constexpr float kUnset = std::numeric_limits<float>::infinity();
    std::vector<float> elevation(grid.nodes(), kUnset);
    Queue queue;
    seedFlood(grid, crust, floor, elevation, queue);
    static constexpr std::array<std::array<int, 2>, 8> kSteps = {
        {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}}};
    while (!queue.empty()) {
        const Wave wave = queue.top();
        queue.pop();
        if (wave.elevation > elevation[wave.node]) {
            continue; // an older entry of a node lowered since
        }
        const int i = grid.column(wave.node);
        const int j = grid.row(wave.node);
        for (const auto& [di, dj] : kSteps) {
            const std::size_t n = grid.index(i + di, j + dj);
            if (crust.land[n] == 0 || elevation[n] != kUnset) {
                continue;
            }
            const double run = dmath::hypot(di * grid.sizeX, dj * grid.sizeZ);
            elevation[n] = std::max(
                floor[n], static_cast<float>(wave.elevation + params.drainSlope * run));
            queue.push({elevation[n], n});
        }
    }
    for (float& e : elevation) {
        e = e == kUnset ? 1.0f : std::max(e, 1.0f);
    }
    return elevation;
}

} // namespace terrain::geography

#include "geography_stages.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

namespace terrain::geography {

namespace {

// Mid-ocean ridges rise beyond the shelf, over this distance.
constexpr double kRidgeFade = 300'000.0;

// The uplift of the provinces fades out under that of the belts, by this many meters of it.
constexpr double kProvincesUnder = 1500.0;

float step(double edge0, double edge1, double x) {
    return noise::smoothstep(static_cast<float>(edge0), static_cast<float>(edge1),
                             static_cast<float>(x));
}

// A node waiting in the flood, ordered by key (its elevation, jittered) then index: a strict
// total order, so the flood visits the nodes in one order whatever the queue does with equal
// keys.
struct Wave {
    float key;
    std::size_t node;

    friend bool operator>(const Wave& a, const Wave& b) {
        return a.key > b.key || (a.key == b.key && a.node > b.node);
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

std::vector<float> oceanFloor(const GeographyParams& params, const Crust& crust,
                              const Tectonics& tectonics) {
    const double shelf = static_cast<double>(params.shelfDepth);
    std::vector<float> floor(crust.coast.size(), params.shelfDepth);
    for (std::size_t k = 0; k < floor.size(); ++k) {
        if (crust.land[k] != 0) {
            continue;
        }
        const double d = -static_cast<double>(crust.coast[k]);
        double depth = d <= params.shelfWidth
                           ? shelf * d / params.shelfWidth
                           : std::max(static_cast<double>(params.abyssDepth),
                                      shelf - (d - params.shelfWidth) * params.shelfSlope);
        depth += static_cast<double>(params.ridgeLift * tectonics.ridge[k]) *
                 step(params.shelfWidth, params.shelfWidth + kRidgeFade, d);
        depth -= static_cast<double>(params.trenchDepth * tectonics.trench[k]) *
                 step(0.0, params.shelfWidth, d);
        floor[k] = static_cast<float>(std::min(depth, shelf));
    }
    return floor;
}

LandFloor landFloor(const GeographyParams& params, const Crust& crust, const Tectonics& tectonics,
                    const Provinces& provinces) {
    LandFloor out;
    out.floor.assign(crust.coast.size(), 1.0f);
    out.rift.assign(crust.coast.size(), 0.0f);
    for (std::size_t k = 0; k < out.floor.size(); ++k) {
        if (crust.land[k] == 0) {
            continue;
        }
        const double tilt = params.inlandRise * std::max(0.0, static_cast<double>(crust.coast[k]));
        const float uplift = tectonics.uplift[k];
        const double province = static_cast<double>(provinces.uplift[k]) *
                                (1.0 - step(0.0, kProvincesUnder, static_cast<double>(uplift)));
        const double base = std::max(1.0, 1.0 + tilt + static_cast<double>(uplift) + province);
        double shoulders = 0.0;
        if (tectonics.shoulder[k] > 0.0f) {
            const double depth = static_cast<double>(tectonics.riftDepth[k]);
            const double height = std::max(
                static_cast<double>(params.riftShoulder),
                (depth + static_cast<double>(params.landMin) + kRiftMargin - base) / kRimShare);
            shoulders = height * static_cast<double>(tectonics.shoulder[k]);
            out.rift[k] = static_cast<float>(base + kRimShare * height - depth);
        }
        out.floor[k] = static_cast<float>(base + shoulders);
    }
    return out;
}

namespace {

// The first neighbor of a coast node at sea along the 4 axes (-1 without any).
std::int8_t seaStep(const Grid& grid, const std::vector<std::uint8_t>& land, std::size_t node) {
    for (std::int8_t k = 0; k < 4; ++k) {
        const auto& [di, dj] = kSteps[static_cast<std::size_t>(k)];
        if (land[grid.index(grid.column(node) + di, grid.row(node) + dj)] == 0) {
            return k;
        }
    }
    return -1;
}

// The area drained through each land node: its own, plus that of every node upstream (the nodes
// in the reverse order of the flood, each giving its area to the node downstream).
std::vector<float> drainedArea(const Grid& grid, const std::vector<std::uint8_t>& land,
                               const std::vector<std::int8_t>& down,
                               const std::vector<std::size_t>& order) {
    std::vector<double> area(grid.nodes(), 0.0);
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        area[*it] += grid.sizeX * grid.sizeZ;
        const auto& [di, dj] = kSteps[static_cast<std::size_t>(down[*it])];
        const std::size_t next = grid.index(grid.column(*it) + di, grid.row(*it) + dj);
        if (land[next] != 0) {
            area[next] += area[*it];
        }
    }
    std::vector<float> out(grid.nodes());
    for (std::size_t k = 0; k < out.size(); ++k) {
        out[k] = static_cast<float>(area[k]);
    }
    return out;
}

} // namespace

Drainage drainedLand(std::uint64_t seed, const GeographyParams& params, const Grid& grid,
                     const Crust& crust, const std::vector<float>& floor) {
    // Up to drainJitter added to the keys of the nodes: where the land is gentle the flood
    // reaches them in a random order (an invasion percolation): its tree branches.
    const auto jitter = [seed, &params](std::size_t node) {
        return params.drainJitter *
               static_cast<float>(static_cast<double>(noise::deriveSeed(seed, node) >> 11) *
                                  0x1.0p-53);
    };
    constexpr float kUnset = std::numeric_limits<float>::infinity();
    Drainage d;
    d.elevation.assign(grid.nodes(), kUnset);
    d.down.assign(grid.nodes(), -1);
    Queue queue;
    seedFlood(grid, crust, floor, d.elevation, queue);
    std::vector<std::size_t> order; // the land nodes, as the flood reaches them
    order.reserve(grid.nodes());
    while (!queue.empty()) {
        const Wave wave = queue.top();
        queue.pop();
        const float here = d.elevation[wave.node];
        if (crust.land[wave.node] != 0 && d.down[wave.node] < 0) {
            d.down[wave.node] = seaStep(grid, crust.land, wave.node); // a seed: the coast
        }
        if (crust.land[wave.node] != 0 && d.down[wave.node] >= 0) {
            order.push_back(wave.node);
        }
        const int i = grid.column(wave.node);
        const int j = grid.row(wave.node);
        for (std::size_t k = 0; k < kSteps.size(); ++k) {
            const auto& [di, dj] = kSteps[k];
            const std::size_t n = grid.index(i + di, j + dj);
            if (crust.land[n] == 0 || d.elevation[n] != kUnset) {
                continue;
            }
            const double run = dmath::hypot(di * grid.sizeX, dj * grid.sizeZ);
            d.elevation[n] =
                std::max(floor[n], static_cast<float>(here + params.drainSlope * run));
            d.down[n] = static_cast<std::int8_t>(k ^ 1); // the opposite step (pairs in kSteps)
            queue.push({d.elevation[n] + jitter(n), n});
        }
    }
    for (float& e : d.elevation) {
        e = e == kUnset ? 1.0f : std::max(e, 1.0f);
    }
    d.area = drainedArea(grid, crust.land, d.down, order);
    return d;
}

} // namespace terrain::geography

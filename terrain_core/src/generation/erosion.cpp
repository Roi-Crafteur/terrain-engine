#include "terrain/generation/erosion.hpp"

#include "grid_neighbors.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace terrain {

namespace {

// Cells below sea level when erosion starts: they are never modified.
std::vector<bool> oceanMask(const HeightGrid& grid) {
    std::vector<bool> ocean(grid.heights.size());
    for (std::size_t i = 0; i < grid.heights.size(); ++i) {
        ocean[i] = grid.heights[i] < kSeaLevel;
    }
    return ocean;
}

// Cells within brushRadius of the droplet cell, weighted by radius - distance.
struct BrushCell {
    int dx;
    int dz;
    float weight;
};

std::vector<BrushCell> makeBrush(int radius) {
    std::vector<BrushCell> brush;
    const auto r = static_cast<float>(radius);
    for (int dz = -radius; dz <= radius; ++dz) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const float distance = std::sqrt(static_cast<float>(dx * dx + dz * dz));
            if (distance < r) {
                brush.push_back({dx, dz, r - distance});
            }
        }
    }
    if (brush.empty()) {
        brush.push_back({0, 0, 1.0f}); // radius 0: the droplet cell only
    }
    return brush;
}

struct HeightAndGradient {
    float height; // meters
    float gx;     // meters per cell along +X
    float gz;     // meters per cell along +Z
};

// Bilinear interpolation of the grid at (x, z) in cells, with 0 <= x, z < size - 1.
HeightAndGradient sampleBilinear(const HeightGrid& grid, float x, float z) {
    const int i = static_cast<int>(x);
    const int j = static_cast<int>(z);
    const float u = x - static_cast<float>(i);
    const float v = z - static_cast<float>(j);
    const float h00 = grid.at(i, j);
    const float h10 = grid.at(i + 1, j);
    const float h01 = grid.at(i, j + 1);
    const float h11 = grid.at(i + 1, j + 1);
    return {
        .height = h00 * (1 - u) * (1 - v) + h10 * u * (1 - v) + h01 * (1 - u) * v + h11 * u * v,
        .gx = (h10 - h00) * (1 - v) + (h11 - h01) * v,
        .gz = (h01 - h00) * (1 - u) + (h11 - h10) * u,
    };
}

struct Droplet {
    float x;
    float z;
    float dirX = 0.0f;
    float dirZ = 0.0f;
    float speed = 1.0f;
    float water = 1.0f;
    float sediment = 0.0f; // meters of ground carried
};

class DropletSimulation {
public:
    DropletSimulation(HeightGrid& grid, const ErosionParams& params)
        : grid_(grid), params_(params), ocean_(oceanMask(grid)),
          brush_(makeBrush(params.brushRadius)),
          cellSize_(static_cast<float>(grid.metersPerPixel)) {}

    // True if (x, z) lies in a grid cell whose four corners are land.
    [[nodiscard]] bool inLandCell(float x, float z) const {
        const float last = static_cast<float>(grid_.size - 1);
        if (!(x >= 0.0f && z >= 0.0f && x < last && z < last)) {
            return false;
        }
        const int i = static_cast<int>(x);
        const int j = static_cast<int>(z);
        return !ocean_[grid_.index(i, j)] && !ocean_[grid_.index(i + 1, j)] &&
               !ocean_[grid_.index(i, j + 1)] && !ocean_[grid_.index(i + 1, j + 1)];
    }

    void run(Droplet d) {
        for (int step = 0; step < params_.maxSteps; ++step) {
            const HeightAndGradient here = sampleBilinear(grid_, d.x, d.z);
            d.dirX = d.dirX * params_.inertia - here.gx * (1.0f - params_.inertia);
            d.dirZ = d.dirZ * params_.inertia - here.gz * (1.0f - params_.inertia);
            const float length = std::sqrt(d.dirX * d.dirX + d.dirZ * d.dirZ);
            if (length < 1.0e-6f) {
                return; // flat ground: the water soaks in where it fell
            }
            d.dirX /= length;
            d.dirZ /= length;
            const float nextX = d.x + d.dirX;
            const float nextZ = d.z + d.dirZ;
            if (!inLandCell(nextX, nextZ)) {
                return; // left the region or reached the ocean: the sediment is lost
            }
            const float drop = here.height - sampleBilinear(grid_, nextX, nextZ).height;
            const float slope = drop / cellSize_;
            const float capacity = std::max(slope, params_.minCapacitySlope) * d.speed * d.water *
                                   params_.capacityFactor * cellSize_;
            if (drop < 0.0f || d.sediment > capacity) {
                // Fill the pit in front of the droplet, or drop what it cannot carry.
                const float amount = drop < 0.0f ? std::min(-drop, d.sediment)
                                                 : (d.sediment - capacity) * params_.depositRate;
                d.sediment -= amount;
                deposit(d.x, d.z, amount);
            } else {
                // Weaker near the border, where the droplets leave the region with their
                // sediment: otherwise the outlets would be dug ever deeper.
                const float weight = borderWeight(static_cast<int>(d.x), static_cast<int>(d.z),
                                                  grid_.size, params_.borderMargin);
                const float amount =
                    weight * std::min((capacity - d.sediment) * params_.erodeRate, drop);
                d.sediment += erode(d.x, d.z, amount);
            }
            const float speed2 = d.speed * d.speed + slope * params_.gravity;
            d.speed = std::min(std::sqrt(std::max(speed2, 0.0f)), params_.maxSpeed);
            d.water *= 1.0f - params_.evaporation;
            d.x = nextX;
            d.z = nextZ;
        }
    }

private:
    // Spreads amount over the four (land) corners of the cell holding (x, z).
    void deposit(float x, float z, float amount) {
        const int i = static_cast<int>(x);
        const int j = static_cast<int>(z);
        const float u = x - static_cast<float>(i);
        const float v = z - static_cast<float>(j);
        grid_.at(i, j) += amount * (1 - u) * (1 - v);
        grid_.at(i + 1, j) += amount * u * (1 - v);
        grid_.at(i, j + 1) += amount * (1 - u) * v;
        grid_.at(i + 1, j + 1) += amount * u * v;
    }

    // Removes up to amount from the land cells under the brush, never below sea level.
    // Returns what was removed.
    float erode(float x, float z, float amount) {
        const int i = static_cast<int>(x);
        const int j = static_cast<int>(z);
        float totalWeight = 0.0f;
        for (const BrushCell& cell : brush_) {
            totalWeight += isLand(i + cell.dx, j + cell.dz) ? cell.weight : 0.0f;
        }
        float removed = 0.0f;
        for (const BrushCell& cell : brush_) {
            if (!isLand(i + cell.dx, j + cell.dz)) {
                continue;
            }
            float& height = grid_.at(i + cell.dx, j + cell.dz);
            const float take =
                std::min(amount * cell.weight / totalWeight, std::max(height - kSeaLevel, 0.0f));
            height -= take;
            removed += take;
        }
        return removed;
    }

    [[nodiscard]] bool isLand(int column, int row) const {
        return column >= 0 && row >= 0 && column < grid_.size && row < grid_.size &&
               !ocean_[grid_.index(column, row)];
    }

    HeightGrid& grid_;
    const ErosionParams& params_;
    std::vector<bool> ocean_;
    std::vector<BrushCell> brush_;
    float cellSize_;
};

// Uniform position in [0, extent) from the top 24 bits of a 32-bit half of a hash.
float unitPosition(std::uint64_t bits, float extent) {
    return static_cast<float>(bits >> 8) / 16777216.0f * extent; // 2^24
}

using grid::kNeighbors;

// One Jacobi step of thermal erosion: accumulates the height changes into delta.
void thermalStep(const HeightGrid& grid, const std::vector<bool>& ocean,
                 const ErosionParams& params, std::vector<float>& delta) {
    const auto cellSize = static_cast<float>(grid.metersPerPixel);
    std::fill(delta.begin(), delta.end(), 0.0f);
    for (int row = 0; row < grid.size; ++row) {
        for (int column = 0; column < grid.size; ++column) {
            if (ocean[grid.index(column, row)]) {
                continue;
            }
            const float height = grid.at(column, row);
            std::array<float, kNeighbors.size()> excess{};
            float total = 0.0f;
            float largest = 0.0f;
            for (std::size_t k = 0; k < kNeighbors.size(); ++k) {
                const int c = column + kNeighbors[k].dx;
                const int r = row + kNeighbors[k].dz;
                if (c < 0 || r < 0 || c >= grid.size || r >= grid.size ||
                    ocean[grid.index(c, r)]) {
                    continue;
                }
                const float limit = params.talusSlope * kNeighbors[k].distance * cellSize;
                excess[k] = std::max(height - grid.at(c, r) - limit, 0.0f);
                total += excess[k];
                largest = std::max(largest, excess[k]);
            }
            if (total <= 0.0f) {
                continue;
            }
            const float moved = params.thermalRate * 0.5f * largest;
            delta[grid.index(column, row)] -= moved;
            for (std::size_t k = 0; k < kNeighbors.size(); ++k) {
                if (excess[k] > 0.0f) {
                    const int c = column + kNeighbors[k].dx;
                    const int r = row + kNeighbors[k].dz;
                    delta[grid.index(c, r)] += moved * excess[k] / total;
                }
            }
        }
    }
}

} // namespace

void erodeHydraulic(HeightGrid& grid, std::uint64_t seed, const ErosionParams& params) {
    if (grid.size < 2) {
        return;
    }
    DropletSimulation simulation(grid, params);
    const auto cells = static_cast<double>(grid.size) * static_cast<double>(grid.size);
    const auto count = static_cast<std::uint64_t>(std::llround(cells * params.dropletsPerCell));
    const auto extent = static_cast<float>(grid.size - 1);
    for (std::uint64_t i = 0; i < count; ++i) {
        const std::uint64_t bits = noise::deriveSeed(seed, i);
        const Droplet droplet{.x = unitPosition(bits >> 32, extent),
                              .z = unitPosition(bits & 0xFFFFFFFFULL, extent)};
        if (simulation.inLandCell(droplet.x, droplet.z)) {
            simulation.run(droplet);
        }
    }
}

void smoothSteepSlopes(HeightGrid& grid, const ErosionParams& params) {
    const std::vector<bool> ocean = oceanMask(grid);
    std::vector<float> delta(grid.heights.size());
    for (int iteration = 0; iteration < params.thermalIterations; ++iteration) {
        thermalStep(grid, ocean, params, delta);
        for (std::size_t i = 0; i < grid.heights.size(); ++i) {
            if (!ocean[i] && delta[i] != 0.0f) {
                grid.heights[i] = std::max(grid.heights[i] + delta[i], kSeaLevel);
            }
        }
    }
}

void erodeHeightGrid(HeightGrid& grid, std::uint64_t seed, const ErosionParams& params) {
    const std::vector<float> raw = grid.heights;
    erodeHydraulic(grid, seed, params);
    smoothSteepSlopes(grid, params);
    for (int row = 0; row < grid.size; ++row) {
        for (int column = 0; column < grid.size; ++column) {
            const std::size_t i = grid.index(column, row);
            const float weight = borderWeight(column, row, grid.size, params.borderMargin);
            const float blended = raw[i] + weight * (grid.heights[i] - raw[i]);
            grid.heights[i] = raw[i] < kSeaLevel ? raw[i] : std::max(blended, kSeaLevel);
        }
    }
}

float borderWeight(int column, int row, int size, int borderMargin) {
    const int distance = std::min({column, row, size - 1 - column, size - 1 - row});
    if (distance <= 0) {
        return 0.0f;
    }
    if (borderMargin <= 1) {
        return 1.0f;
    }
    return noise::smoothstep(0.0f, static_cast<float>(borderMargin), static_cast<float>(distance));
}

} // namespace terrain

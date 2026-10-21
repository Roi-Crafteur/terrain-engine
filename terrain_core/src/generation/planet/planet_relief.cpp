#include "terrain/generation/planet/planet_relief.hpp"

#include "planet_parallel.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace terrain::planet {

namespace {

constexpr int kMaxSamplesPerAxis = 8;

float cellHeight(const Heightfield& field, const PlanetGrid& grid, int i, int j, int samples) {
    if (samples == 1) {
        return field.heightAt(grid.centerX(i), grid.centerZ(j));
    }
    const double x0 = grid.centerX(i) - 0.5 * grid.cellSize;
    const double z0 = grid.centerZ(j) - 0.5 * grid.cellSize;
    const double step = grid.cellSize / samples;
    double sum = 0.0;
    for (int b = 0; b < samples; ++b) {
        for (int a = 0; a < samples; ++a) {
            sum += field.heightAt(x0 + (a + 0.5) * step, z0 + (b + 0.5) * step);
        }
    }
    return static_cast<float>(sum / (samples * samples));
}

// Sizes of the 8-connected below-sea components, by their first cell (lowest index), and the
// component of each below-sea cell (-1 elsewhere).
struct Components {
    std::vector<std::int32_t> of;
    std::vector<std::size_t> size;
};

Components belowSeaComponents(const PlanetGrid& grid, const std::vector<float>& height) {
    Components result;
    result.of.assign(grid.cells(), -1);
    std::vector<std::size_t> queue;
    for (std::size_t start = 0; start < grid.cells(); ++start) {
        if (!(height[start] < kSeaLevel) || result.of[start] >= 0) {
            continue;
        }
        const auto id = static_cast<std::int32_t>(result.size.size());
        queue.assign(1, start);
        result.of[start] = id;
        for (std::size_t head = 0; head < queue.size(); ++head) {
            for (int k = 0; k < 8; ++k) {
                const std::size_t n = grid.neighbor(queue[head], k);
                if (height[n] < kSeaLevel && result.of[n] < 0) {
                    result.of[n] = id;
                    queue.push_back(n);
                }
            }
        }
        result.size.push_back(queue.size());
    }
    return result;
}

} // namespace

std::vector<float> sampleRelief(const Heightfield& field, const PlanetGrid& grid,
                                int samplesPerAxis, int threads) {
    if (samplesPerAxis < 1 || samplesPerAxis > kMaxSamplesPerAxis) {
        throw std::invalid_argument("sampleRelief: samplesPerAxis must be from 1 to 8");
    }
    if (field.world().x != grid.periodX() || field.world().z != grid.periodZ()) {
        throw std::invalid_argument("sampleRelief: the field and the grid tile different worlds");
    }
    std::vector<float> height(grid.cells());
    parallelRows(grid.nz, resolveThreads(threads), [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < grid.nx; ++i) {
                const float h = cellHeight(field, grid, i, j, samplesPerAxis);
                if (!std::isfinite(h)) {
                    throw std::domain_error("sampleRelief: non-finite altitude");
                }
                height[grid.index(i, j)] = h;
            }
        }
    });
    return height;
}

std::vector<std::uint8_t> markOcean(const PlanetGrid& grid, const std::vector<float>& height,
                                    double minOceanArea) {
    const Components components = belowSeaComponents(grid, height);
    std::vector<std::uint8_t> ocean(grid.cells(), kLand);
    bool any = false;
    for (std::size_t c = 0; c < grid.cells(); ++c) {
        const std::int32_t id = components.of[c];
        if (id >= 0 &&
            static_cast<double>(components.size[static_cast<std::size_t>(id)]) * grid.cellArea() >=
                minOceanArea) {
            ocean[c] = kOcean;
            any = true;
        }
    }
    if (!any && grid.cells() > 0) {
        std::size_t lowest = 0;
        for (std::size_t c = 1; c < grid.cells(); ++c) {
            if (lowerCell(height, c, lowest)) {
                lowest = c;
            }
        }
        ocean[lowest] = kForcedOutlet;
    }
    return ocean;
}

} // namespace terrain::planet

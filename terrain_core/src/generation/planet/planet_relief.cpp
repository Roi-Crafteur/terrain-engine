#include "terrain/generation/planet/planet_relief.hpp"

#include "planet_parallel.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace terrain::planet {

namespace {

constexpr int kMaxSamplesPerAxis = 8;

// The continentalness of the Heightfield is the coast distance over this scale.
constexpr double kContinentalScale = 1'500'000.0;

// The floor of a cell: the lowest of its n^2 points of the same water as its center (the land of a
// cell on the coast, not the bottom of the sea), else its center; its index and whether the
// mountain mask is above 0 there.
struct Floor {
    float height = 0.0f;
    std::uint8_t index = kCenterFloor;
    std::uint8_t mountain = 0;
};

Floor cellFloor(const Heightfield& field, const PlanetGrid& grid, std::size_t cell, int n,
                const HeightSample& center) {
    const bool sea = center.water == WaterKind::Ocean;
    Floor f{center.height, kCenterFloor, center.mountainMask > 0.0f ? std::uint8_t{1}
                                                                     : std::uint8_t{0}};
    bool found = false;
    for (int k = 0; k < n * n; ++k) {
        const std::array<double, 2> p = floorPoint(grid, cell, static_cast<std::uint8_t>(k), n);
        const HeightSample s = field.sampleAt(p[0], p[1]);
        if ((s.water == WaterKind::Ocean) == sea && (!found || s.height < f.height)) {
            found = true;
            f = {s.height, static_cast<std::uint8_t>(k), s.mountainMask > 0.0f ? std::uint8_t{1}
                                                                               : std::uint8_t{0}};
        }
    }
    return f;
}

// The altitude of a cell; with one sample, center holds the sample of its center.
float cellHeight(const Heightfield& field, const PlanetGrid& grid, int i, int j, int samples,
                 const HeightSample& center) {
    if (samples == 1) {
        return center.height;
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

Components belowSeaComponents(const PlanetGrid& grid, const std::vector<std::uint8_t>& below) {
    Components result;
    result.of.assign(grid.cells(), -1);
    std::vector<std::size_t> queue;
    for (std::size_t start = 0; start < grid.cells(); ++start) {
        if (below[start] == 0 || result.of[start] >= 0) {
            continue;
        }
        const auto id = static_cast<std::int32_t>(result.size.size());
        queue.assign(1, start);
        result.of[start] = id;
        for (std::size_t head = 0; head < queue.size(); ++head) {
            for (int k = 0; k < 8; ++k) {
                const std::size_t n = grid.neighbor(queue[head], k);
                if (below[n] != 0 && result.of[n] < 0) {
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
    return sampleCells(field, grid, samplesPerAxis, threads).height;
}

std::uint8_t floorOf(const Heightfield& field, const PlanetGrid& grid, std::size_t cell,
                     int floorSamples) {
    if (floorSamples <= 1) {
        return kCenterFloor;
    }
    const HeightSample center =
        field.sampleAt(grid.centerX(grid.column(cell)), grid.centerZ(grid.row(cell)));
    return cellFloor(field, grid, cell, floorSamples, center).index;
}

std::array<double, 2> floorPoint(const PlanetGrid& grid, std::size_t cell, std::uint8_t floor,
                                 int floorSamples) {
    const double x = grid.centerX(grid.column(cell));
    const double z = grid.centerZ(grid.row(cell));
    if (floor == kCenterFloor) {
        return {x, z};
    }
    const double step = grid.cellSize / floorSamples;
    return {x - 0.5 * grid.cellSize + (floor % floorSamples + 0.5) * step,
            z - 0.5 * grid.cellSize + (floor / floorSamples + 0.5) * step};
}

CellRelief sampleCells(const Heightfield& field, const PlanetGrid& grid, int samplesPerAxis,
                       int threads, int floorSamples) {
    if (samplesPerAxis < 1 || samplesPerAxis > kMaxSamplesPerAxis || floorSamples < 1 ||
        floorSamples > kMaxSamplesPerAxis) {
        throw std::invalid_argument("sampleRelief: samplesPerAxis and floorSamples must be from 1 "
                                    "to 8");
    }
    if (field.world().x != grid.periodX() || field.world().z != grid.periodZ()) {
        throw std::invalid_argument("sampleRelief: the field and the grid tile different worlds");
    }
    CellRelief cells{std::vector<float>(grid.cells()), std::vector<std::uint8_t>(grid.cells()),
                     std::vector<std::uint8_t>(grid.cells(), kCenterFloor),
                     std::vector<std::uint8_t>(grid.cells())};
    const auto nearLand = static_cast<float>(-kFloorCoast / kContinentalScale);
    parallelRows(grid.nz, resolveThreads(threads), [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < grid.nx; ++i) {
                const std::size_t c = grid.index(i, j);
                const HeightSample center = field.sampleAt(grid.centerX(i), grid.centerZ(j));
                float h = cellHeight(field, grid, i, j, samplesPerAxis, center);
                cells.mountain[c] = center.mountainMask > 0.0f ? 1 : 0;
                if (floorSamples > 1 && center.continentalness > nearLand) {
                    const Floor f = cellFloor(field, grid, c, floorSamples, center);
                    h = f.height;
                    cells.floor[c] = f.index;
                    cells.mountain[c] = f.mountain;
                }
                if (!std::isfinite(h)) {
                    throw std::domain_error("sampleRelief: non-finite altitude");
                }
                cells.height[c] = h;
                cells.sea[c] = center.water == WaterKind::Ocean ? 1 : 0;
            }
        }
    });
    return cells;
}

std::vector<std::uint8_t> markOcean(const PlanetGrid& grid, const std::vector<float>& height,
                                    double minOceanArea) {
    std::vector<std::uint8_t> below(grid.cells());
    for (std::size_t c = 0; c < grid.cells(); ++c) {
        below[c] = height[c] < kSeaLevel ? 1 : 0;
    }
    return markOcean(grid, height, below, minOceanArea);
}

std::vector<std::uint8_t> markOcean(const PlanetGrid& grid, const std::vector<float>& height,
                                    const std::vector<std::uint8_t>& sea, double minOceanArea) {
    const Components components = belowSeaComponents(grid, sea);
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

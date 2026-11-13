#include "terrain/generation/planet/planet_relief.hpp"

#include "planet_parallel.hpp"
#include "terrain/generation/planet/planet_land.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstddef>
#include <deque>
#include <stdexcept>

namespace terrain::planet {

namespace {

constexpr int kMaxSamplesPerAxis = 8;

// The continentalness of the Heightfield is the coast distance over this scale.
constexpr double kContinentalScale = 1'500'000.0;

// The floor of a cell: the lowest of its n^2 points of the same water as its center (the land of a
// cell on the coast, not the bottom of the sea), else its center; its index and whether the
// mountain mask is above 0 there.
// The heights of the n^2 points, kept for the exits of the cells under the mountains.
struct Floor {
    float height = 0.0f;
    std::uint8_t index = kCenterFloor;
    std::uint8_t mountain = 0;
    std::array<float, kMaxSamplesPerAxis * kMaxSamplesPerAxis> heights{};
};

Floor cellFloor(const Heightfield& field, const PlanetGrid& grid, std::size_t cell, int n,
                const HeightSample& center) {
    const bool sea = center.water == WaterKind::Ocean;
    Floor f{center.height, kCenterFloor,
            center.mountainMask > 0.0f ? std::uint8_t{1} : std::uint8_t{0}, {}};
    bool found = false;
    for (int k = 0; k < n * n; ++k) {
        const std::array<double, 2> p = floorPoint(grid, cell, static_cast<std::uint8_t>(k), n);
        const HeightSample s = field.sampleAt(p[0], p[1]);
        f.heights[static_cast<std::size_t>(k)] = s.height;
        if ((s.water == WaterKind::Ocean) == sea && (!found || s.height < f.height)) {
            found = true;
            f.height = s.height;
            f.index = static_cast<std::uint8_t>(k);
            f.mountain = s.mountainMask > 0.0f ? std::uint8_t{1} : std::uint8_t{0};
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

// Marks of markOcean during its floods, in its result: kLand and kOcean are final.
constexpr std::uint8_t kUnvisited = 3;
constexpr std::uint8_t kVisited = 4;

// Floods the 8-connected component of start whose cells are marked from, marking them to; its
// size. The queue frees what it has passed: it holds the front of the flood, not the component.
std::size_t floodComponent(const PlanetGrid& grid, std::vector<std::uint8_t>& mark,
                           std::size_t start, std::uint8_t from, std::uint8_t to) {
    std::deque<std::uint32_t> queue{static_cast<std::uint32_t>(start)};
    mark[start] = to;
    std::size_t size = 0;
    while (!queue.empty()) {
        const std::size_t cell = queue.front();
        queue.pop_front();
        ++size;
        for (int k = 0; k < 8; ++k) {
            const std::size_t n = grid.neighbor(cell, k);
            if (mark[n] == from) {
                mark[n] = to;
                queue.push_back(static_cast<std::uint32_t>(n));
            }
        }
    }
    return size;
}

} // namespace

CellExits cellExits(int column, const float* heights, int n, int floorIndex) {
    const int count = n * n;
    std::array<float, kMaxSamplesPerAxis * kMaxSamplesPerAxis> best{};
    best.fill(std::numeric_limits<float>::infinity());
    best[static_cast<std::size_t>(floorIndex)] = heights[floorIndex];
    // The lowest of the highest points of the ways from the floor point: relaxed until stable.
    for (bool changed = true; changed;) {
        changed = false;
        for (int v = 0; v < count; ++v) {
            float low = best[static_cast<std::size_t>(v)];
            for (int db = -1; db <= 1; ++db) {
                for (int da = -1; da <= 1; ++da) {
                    const int a = v % n + da;
                    const int b = v / n + db;
                    if ((da != 0 || db != 0) && a >= 0 && a < n && b >= 0 && b < n) {
                        low = std::min(low, std::max(best[static_cast<std::size_t>(b * n + a)],
                                                     heights[v]));
                    }
                }
            }
            changed = changed || low < best[static_cast<std::size_t>(v)];
            best[static_cast<std::size_t>(v)] = low;
        }
    }
    CellExits e;
    e.column = static_cast<std::uint16_t>(column);
    const float floor = std::floor(heights[floorIndex]);
    e.floor = static_cast<std::int16_t>(std::clamp(floor, -32768.0f, 32767.0f));
    for (std::size_t k = 0; k < kPlanetNeighbors.size(); ++k) {
        float exit = std::numeric_limits<float>::infinity();
        const int dx = kPlanetNeighbors[k].dx;
        const int dz = kPlanetNeighbors[k].dz;
        for (int v = 0; v < count; ++v) {
            const bool alongX = dx == 0 || v % n == (dx > 0 ? n - 1 : 0);
            const bool alongZ = dz == 0 || v / n == (dz > 0 ? n - 1 : 0);
            if (alongX && alongZ) {
                exit = std::min(exit, best[static_cast<std::size_t>(v)]);
            }
        }
        const double steps = std::ceil((static_cast<double>(exit) - e.floor) / kExitStep);
        e.climb[k] = static_cast<std::uint8_t>(std::clamp(steps, 0.0, 255.0));
    }
    return e;
}

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
                       int threads, int floorSamples, bool exits) {
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
                     std::vector<std::uint8_t>(grid.cells()), {}};
    const auto nearLand = static_cast<float>(-kFloorCoast / kContinentalScale);
    cells.exits.resize(exits && floorSamples > 1 ? static_cast<std::size_t>(grid.nz) : 0);
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
                    if (exits && f.mountain != 0 && f.index != kCenterFloor) {
                        const auto points =
                            static_cast<std::ptrdiff_t>(floorSamples * floorSamples);
                        const float top = *std::max_element(f.heights.begin(),
                                                            f.heights.begin() + points);
                        if (static_cast<double>(top - f.height) > kExitLeast) {
                            cells.exits[static_cast<std::size_t>(j)].push_back(
                                cellExits(i, f.heights.data(), floorSamples, f.index));
                        }
                    }
                }
                if (!std::isfinite(h)) {
                    throw std::domain_error("sampleRelief: non-finite altitude");
                }
                cells.height[c] = h;
                cells.sea[c] = center.water == WaterKind::Ocean ? 1 : 0;
            }
        }
    });
    for (std::vector<CellExits>& row : cells.exits) {
        row.shrink_to_fit();
    }
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
    // Each component is flooded once to count it, then again to mark it ocean or land.
    std::vector<std::uint8_t> ocean(grid.cells());
    for (std::size_t c = 0; c < grid.cells(); ++c) {
        ocean[c] = sea[c] != 0 ? kUnvisited : std::uint8_t{kLand};
    }
    bool any = false;
    for (std::size_t start = 0; start < grid.cells(); ++start) {
        if (ocean[start] != kUnvisited) {
            continue;
        }
        const std::size_t size = floodComponent(grid, ocean, start, kUnvisited, kVisited);
        const bool big = static_cast<double>(size) * grid.cellArea() >= minOceanArea;
        const std::uint8_t kind = big ? std::uint8_t{kOcean} : std::uint8_t{kLand};
        floodComponent(grid, ocean, start, kVisited, kind);
        any = any || big;
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

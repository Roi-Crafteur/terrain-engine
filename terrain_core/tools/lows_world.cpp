// The land below the sea level on a grid of the whole world (lows_world.hpp).
#include "lows_world.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace lows_tool {

namespace {

using terrain::ReliefGrid;
using terrain::WaterKind;

bool dryBelow(const ReliefGrid& grid, std::size_t cell) {
    return !grid.seaAt(cell) && grid.heights[cell] < terrain::kSeaLevel &&
           grid.water[cell] == WaterKind::None;
}

// The median of values (reordered), 0 for none.
float median(std::vector<float>& values) {
    if (values.empty()) {
        return 0.0f;
    }
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

void countWater(WaterKind water, WorldFigures& out) {
    switch (water) {
    case WaterKind::None:
        ++out.dryBelow;
        break;
    case WaterKind::SaltLake:
        ++out.saltBelow;
        break;
    case WaterKind::Lake:
        ++out.lakeBelow;
        break;
    default:
        ++out.riverBelow;
        break;
    }
}

// The stretch of dry land below the sea level holding start, its samples marked in seen.
Stretch stretchFrom(const ReliefGrid& grid, std::size_t start, std::vector<std::uint8_t>& seen) {
    Stretch s;
    s.lowest = grid.heights[start];
    s.lowX = grid.x(static_cast<int>(start % static_cast<std::size_t>(grid.columns)));
    s.lowZ = grid.z(static_cast<int>(start / static_cast<std::size_t>(grid.columns)));
    std::vector<std::size_t> stack{start};
    seen[start] = 1;
    while (!stack.empty()) {
        const std::size_t cell = stack.back();
        stack.pop_back();
        ++s.samples;
        const int i = static_cast<int>(cell % static_cast<std::size_t>(grid.columns));
        const int j = static_cast<int>(cell / static_cast<std::size_t>(grid.columns));
        if (grid.heights[cell] < s.lowest) {
            s.lowest = grid.heights[cell];
            s.lowX = grid.x(i);
            s.lowZ = grid.z(j);
        }
        for (int b = -1; b <= 1; ++b) {
            for (int a = -1; a <= 1; ++a) {
                const int ni = (i + a + grid.columns) % grid.columns;
                const int nj = (j + b + grid.rows) % grid.rows;
                const std::size_t next = grid.index(ni, nj);
                if (seen[next] == 0 && dryBelow(grid, next)) {
                    seen[next] = 1;
                    stack.push_back(next);
                }
            }
        }
    }
    s.area = static_cast<double>(s.samples) * grid.spacing * grid.spacing;
    return s;
}

std::vector<Stretch> largestStretches(const ReliefGrid& grid, std::size_t keep) {
    std::vector<std::uint8_t> seen(grid.heights.size(), 0);
    std::vector<Stretch> all;
    for (std::size_t cell = 0; cell < grid.heights.size(); ++cell) {
        if (seen[cell] == 0 && dryBelow(grid, cell)) {
            all.push_back(stretchFrom(grid, cell, seen));
        }
    }
    // The largest first, then the lowest, then the first met: the same order on every run.
    std::stable_sort(all.begin(), all.end(), [](const Stretch& a, const Stretch& b) {
        return a.samples > b.samples || (a.samples == b.samples && a.lowest < b.lowest);
    });
    if (all.size() > keep) {
        all.resize(keep);
    }
    return all;
}

// Classes of a sample on the map, the rarest last.
enum Class : int { kSea, kLand, kWaterBelow, kSaltBelow, kDryBelow };

Class classOf(const ReliefGrid& grid, std::size_t cell) {
    if (grid.seaAt(cell)) {
        return kSea;
    }
    if (!(grid.heights[cell] < terrain::kSeaLevel)) {
        return kLand;
    }
    switch (grid.water[cell]) {
    case WaterKind::None:
        return kDryBelow;
    case WaterKind::SaltLake:
        return kSaltBelow;
    default:
        return kWaterBelow;
    }
}

std::array<std::uint8_t, 3> colorOf(Class c, double meanLand) {
    switch (c) {
    case kSea:
        return {18, 32, 64};
    case kDryBelow:
        return {230, 30, 30};
    case kSaltBelow:
        return {250, 250, 245};
    case kWaterBelow:
        return {40, 220, 230};
    default: {
        const double t = std::clamp(meanLand / 3000.0, 0.0, 1.0);
        const auto g = static_cast<std::uint8_t>(std::lround(90.0 + 120.0 * std::sqrt(t)));
        return {g, g, g};
    }
    }
}

} // namespace

void addPlanetWater(ReliefGrid& grid, const terrain::PlanetLevel& planet,
                    std::size_t& riverCells) {
    riverCells = 0;
    for (int j = 0; j < grid.rows; ++j) {
        for (int i = 0; i < grid.columns; ++i) {
            const std::size_t cell = grid.index(i, j);
            if (grid.seaAt(cell) || !(grid.heights[cell] < terrain::kSeaLevel)) {
                continue;
            }
            const terrain::PlanetSample p = planet.sampleAt(grid.x(i), grid.z(j));
            riverCells += p.discharge > 0.0f ? 1 : 0;
            if (p.lake < 0) {
                continue;
            }
            const terrain::PlanetLake& lake = planet.lakes()[static_cast<std::size_t>(p.lake)];
            if (grid.heights[cell] < lake.level) {
                grid.water[cell] = lake.overflowing || lake.frozen ? WaterKind::Lake
                                                                  : WaterKind::SaltLake;
                grid.waterLevels[cell] = lake.level;
            }
        }
    }
}

WorldFigures measureWorld(const ReliefGrid& grid, std::size_t keep) {
    if (!grid.wraps || grid.water.size() != grid.heights.size()) {
        throw std::invalid_argument("measureWorld: a wrapping grid with its water is expected");
    }
    WorldFigures out;
    out.samples = grid.heights.size();
    std::vector<float> land;
    std::vector<float> depths;
    for (std::size_t cell = 0; cell < grid.heights.size(); ++cell) {
        if (grid.seaAt(cell)) {
            continue;
        }
        const float h = grid.heights[cell];
        ++out.land;
        land.push_back(h);
        out.below50 += h < 50.0f ? 1 : 0;
        if (h < terrain::kSeaLevel) {
            ++out.below0;
            depths.push_back(-h);
            countWater(grid.water[cell], out);
        }
    }
    out.medianLand = median(land);
    out.medianDepth = median(depths);
    out.largest = largestStretches(grid, keep);
    return out;
}

terrain::Image lowsMap(const ReliefGrid& grid, int width) {
    if (width < 1 || grid.columns % width != 0) {
        throw std::invalid_argument("lowsMap: the width must divide the columns of the grid");
    }
    const int factor = grid.columns / width;
    terrain::Image image;
    image.width = width;
    image.height = grid.rows / factor;
    image.channels = 3;
    image.pixels.resize(static_cast<std::size_t>(image.width) *
                        static_cast<std::size_t>(image.height) * 3U);
    for (int py = 0; py < image.height; ++py) {
        for (int px = 0; px < image.width; ++px) {
            Class rarest = kSea;
            double sum = 0.0;
            int landSamples = 0;
            for (int b = 0; b < factor; ++b) {
                for (int a = 0; a < factor; ++a) {
                    const std::size_t cell = grid.index(px * factor + a, py * factor + b);
                    const Class c = classOf(grid, cell);
                    rarest = std::max(rarest, c);
                    if (c == kLand) {
                        sum += static_cast<double>(grid.heights[cell]);
                        ++landSamples;
                    }
                }
            }
            const auto rgb = colorOf(rarest, landSamples > 0 ? sum / landSamples : 0.0);
            const std::size_t at = (static_cast<std::size_t>(py) *
                                        static_cast<std::size_t>(image.width) +
                                    static_cast<std::size_t>(px)) *
                                   3U;
            std::copy(rgb.begin(), rgb.end(), image.pixels.begin() + static_cast<std::ptrdiff_t>(at));
        }
    }
    return image;
}

} // namespace lows_tool

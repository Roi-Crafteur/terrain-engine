#include "terrain/io/relief_stats.hpp"

#include "../generation/planet/planet_parallel.hpp"
#include "terrain/det_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace terrain {

namespace {

constexpr double kStep = 1000.0;      // plains and mountains: 1 km between samples
constexpr int kSide = 11;             // over 10 km
constexpr double kCoastStep = 200.0;  // coasts: 200 m between samples
constexpr int kCoastSide = 201;       // over 40 km
constexpr double kMinCoast = 20000.0; // shorter coast lines in a window are not measured

// Up to count cells holding bits, spread evenly over all of them.
std::vector<std::size_t> spreadCells(const ReliefStats& stats, unsigned bits, int count) {
    std::vector<std::size_t> all;
    for (std::size_t cell = 0; cell < stats.cells.size(); ++cell) {
        if ((stats.cells[cell] & bits) == bits) {
            all.push_back(cell);
        }
    }
    std::vector<std::size_t> picked;
    if (all.empty() || count <= 0) {
        return picked;
    }
    const std::size_t n = std::min(all.size(), static_cast<std::size_t>(count));
    for (std::size_t k = 0; k < n; ++k) {
        picked.push_back(all[k * all.size() / n]);
    }
    return picked;
}

double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const auto k = static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1));
    return values[k];
}

struct WindowFigures {
    double slope = 0.0;
    double relief = 0.0;
};

WindowFigures figures(const ReliefGrid& window) {
    WindowFigures f;
    const auto [low, high] = std::minmax_element(window.heights.begin(), window.heights.end());
    f.relief = static_cast<double>(*high - *low);
    double sum = 0.0;
    int count = 0;
    for (int j = 1; j + 1 < window.rows; ++j) {
        for (int i = 1; i + 1 < window.columns; ++i) {
            const double gx = (window.at(i + 1, j) - window.at(i - 1, j)) / (2.0 * window.spacing);
            const double gz = (window.at(i, j + 1) - window.at(i, j - 1)) / (2.0 * window.spacing);
            sum += dmath::hypot(gx, gz);
            ++count;
        }
    }
    f.slope = count > 0 ? sum / count : 0.0;
    return f;
}

// L(1 km) / L(10 km) of the longest coast line of the window, or 0 without a long enough one.
double sinuosity(const ReliefGrid& window) {
    const std::vector<relief_shapes::Line> lines = relief_shapes::coastLines(window);
    const relief_shapes::Line* longest = nullptr;
    double length = 0.0;
    for (const relief_shapes::Line& line : lines) {
        const double l = relief_shapes::lineLength(line);
        if (l > length) {
            longest = &line;
            length = l;
        }
    }
    if (longest == nullptr || length < kMinCoast) {
        return 0.0;
    }
    const double coarse = relief_shapes::dividerLength(*longest, 10.0 * kStep);
    return coarse > 0.0 ? relief_shapes::dividerLength(*longest, kStep) / coarse : 0.0;
}

// The windows around the cells, one per block on the threads.
std::vector<ReliefGrid> windows(const Heightfield& field, const ReliefGrid& world,
                                const std::vector<std::size_t>& cells, double step, int side,
                                int threads) {
    std::vector<ReliefGrid> out(cells.size());
    planet::parallelBlocks(static_cast<int>(cells.size()), 1, planet::resolveThreads(threads),
                           [&](int begin, int end) {
                               for (int k = begin; k < end; ++k) {
                                   const std::size_t cell = cells[static_cast<std::size_t>(k)];
                                   const auto columns =
                                       static_cast<std::size_t>(world.columns);
                                   const int i = static_cast<int>(cell % columns);
                                   const int j = static_cast<int>(cell / columns);
                                   out[static_cast<std::size_t>(k)] = sampleReliefWindow(
                                       field, world.x(i), world.z(j), step, side, side, 1);
                               }
                           });
    return out;
}

// Up to count land cells in the mountain belts of the geography (belt of at least 0.5), spread
// evenly over all of them; none without the geography.
std::vector<std::size_t> beltCells(const Heightfield& field, const ReliefGrid& world,
                                   const ReliefStats& stats, int count) {
    std::vector<std::size_t> all;
    const Geography* geography = field.geography();
    for (std::size_t cell = 0; geography != nullptr && cell < stats.cells.size(); ++cell) {
        if ((stats.cells[cell] & kReliefLand) == 0U) {
            continue;
        }
        const auto columns = static_cast<std::size_t>(world.columns);
        const double x = world.x(static_cast<int>(cell % columns));
        const double z = world.z(static_cast<int>(cell / columns));
        if (geography->sampleAt(wrapCoordinate(x, field.world().x),
                                wrapCoordinate(z, field.world().z))
                .belt >= 0.5f) {
            all.push_back(cell);
        }
    }
    std::vector<std::size_t> picked;
    const std::size_t n = std::min(all.size(), static_cast<std::size_t>(std::max(count, 0)));
    for (std::size_t k = 0; k < n; ++k) {
        picked.push_back(all[k * all.size() / n]);
    }
    return picked;
}

} // namespace

LocalReliefStats measureLocalRelief(const Heightfield& field, const ReliefGrid& world,
                                    const ReliefStats& stats, int windowCount, int threads) {
    LocalReliefStats local;
    std::vector<double> slopes;
    std::vector<double> reliefs;
    for (const ReliefGrid& w : windows(field, world, spreadCells(stats, kReliefPlain, windowCount),
                                       kStep, kSide, threads)) {
        const WindowFigures f = figures(w);
        slopes.push_back(f.slope);
        reliefs.push_back(f.relief);
    }
    local.plainWindows = static_cast<int>(slopes.size());
    local.plainSlope = percentile(slopes, 0.5);
    local.plainRelief = percentile(reliefs, 0.5);
    reliefs.clear();
    for (const ReliefGrid& w :
         windows(field, world, spreadCells(stats, kReliefMountain, windowCount), kStep, kSide,
                 threads)) {
        reliefs.push_back(figures(w).relief);
    }
    local.mountainWindows = static_cast<int>(reliefs.size());
    local.mountainRelief = percentile(reliefs, 0.5);
    local.mountainRelief90 = percentile(reliefs, 0.9);
    reliefs.clear();
    for (const ReliefGrid& w : windows(field, world, beltCells(field, world, stats, windowCount),
                                       kStep, kSide, threads)) {
        reliefs.push_back(figures(w).relief);
    }
    local.beltWindows = static_cast<int>(reliefs.size());
    local.beltRelief = percentile(reliefs, 0.5);
    local.beltRelief90 = percentile(reliefs, 0.9);
    std::vector<double> ratios;
    for (const ReliefGrid& w : windows(field, world, spreadCells(stats, kReliefCoast, windowCount),
                                       kCoastStep, kCoastSide, threads)) {
        const double ratio = sinuosity(w);
        if (ratio > 0.0) {
            ratios.push_back(ratio);
        }
    }
    local.coastWindows = static_cast<int>(ratios.size());
    local.coastSinuosity = percentile(ratios, 0.5);
    return local;
}

} // namespace terrain

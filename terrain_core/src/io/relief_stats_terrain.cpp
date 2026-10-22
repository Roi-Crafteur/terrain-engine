#include "terrain/io/relief_stats.hpp"

#include "../generation/planet/planet_parallel.hpp"
#include "relief_stats_support.hpp"
#include "terrain/det_math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace terrain {

namespace {

using relief_support::percentile;
using relief_support::spread;
using relief_support::windows;

constexpr double kTan30 = 0.57735026918962576451;
constexpr double kTan45 = 1.0;
constexpr double kHighland = 2500.0; // high plateaus: land at least this high

// The belts: those of the geography, else the mountain cells.
std::vector<std::size_t> beltsOf(const Heightfield& field, const ReliefGrid& world,
                                 const ReliefStats& stats) {
    if (field.geography() != nullptr) {
        return relief_support::beltCells(field, world, stats);
    }
    std::vector<std::size_t> all;
    for (std::size_t cell = 0; cell < stats.cells.size(); ++cell) {
        if ((stats.cells[cell] & kReliefMountain) != 0U) {
            all.push_back(cell);
        }
    }
    return all;
}

std::vector<std::size_t> cellsWith(const ReliefStats& stats, unsigned bits) {
    std::vector<std::size_t> all;
    for (std::size_t cell = 0; cell < stats.cells.size(); ++cell) {
        if ((stats.cells[cell] & bits) == bits) {
            all.push_back(cell);
        }
    }
    return all;
}

double relief(const ReliefGrid& window) {
    const auto [low, high] = std::minmax_element(window.heights.begin(), window.heights.end());
    return static_cast<double>(*high - *low);
}

// Standard deviation of the heights of the window around their mean.
double deviation(const ReliefGrid& window) {
    double sum = 0.0;
    for (const float h : window.heights) {
        sum += static_cast<double>(h);
    }
    const double mean = sum / static_cast<double>(window.heights.size());
    double squares = 0.0;
    for (const float h : window.heights) {
        squares += (static_cast<double>(h) - mean) * (static_cast<double>(h) - mean);
    }
    return std::sqrt(squares / static_cast<double>(window.heights.size()));
}

// 10th percentile, median and 90th percentile of the deviations of the windows.
std::array<double, 3> deviations(const std::vector<ReliefGrid>& windows) {
    std::vector<double> values;
    for (const ReliefGrid& w : windows) {
        values.push_back(deviation(w));
    }
    return {percentile(values, 0.1), percentile(values, 0.5), percentile(values, 0.9)};
}

// Shares of the slopes of the window above 30 and 45 degrees (central differences).
void addSlopes(const ReliefGrid& w, double& steep, double& cliff, double& count) {
    for (int j = 1; j + 1 < w.rows; ++j) {
        for (int i = 1; i + 1 < w.columns; ++i) {
            const double gx = (w.at(i + 1, j) - w.at(i - 1, j)) / (2.0 * w.spacing);
            const double gz = (w.at(i, j + 1) - w.at(i, j - 1)) / (2.0 * w.spacing);
            const double slope = dmath::hypot(gx, gz);
            steep += slope > kTan30 ? 1.0 : 0.0;
            cliff += slope > kTan45 ? 1.0 : 0.0;
            count += 1.0;
        }
    }
}

void measureBelts(const Heightfield& field, const ReliefGrid& world,
                  const std::vector<std::size_t>& belts, int count, int threads,
                  TerrainFigures& f) {
    const std::vector<std::size_t> cells = spread(belts, count);
    std::vector<double> reliefs;
    for (const ReliefGrid& w : windows(field, world, cells, 500.0, 11, threads)) {
        reliefs.push_back(relief(w));
    }
    f.beltWindows = static_cast<int>(reliefs.size());
    f.beltRelief5 = percentile(reliefs, 0.5);
    double steep = 0.0;
    double cliff = 0.0;
    double samples = 0.0;
    for (const ReliefGrid& w : windows(field, world, cells, 25.0, 81, threads)) {
        addSlopes(w, steep, cliff, samples);
    }
    f.steepShare = samples > 0.0 ? steep / samples : 0.0;
    f.cliffShare = samples > 0.0 ? cliff / samples : 0.0;
}

void measurePlains(const Heightfield& field, const ReliefGrid& world, const ReliefStats& stats,
                   int count, int threads, TerrainFigures& f) {
    const std::vector<std::size_t> cells = spread(cellsWith(stats, kReliefPlain), count);
    std::vector<double> near;
    for (const ReliefGrid& w : windows(field, world, cells, 100.0, 11, threads)) {
        near.push_back(relief(w));
    }
    f.plainNear10 = percentile(near, 0.1);
    f.plainNear50 = percentile(near, 0.5);
    f.plainDeviation200 = deviations(windows(field, world, cells, 10.0, 21, threads));
    f.plainDeviation1k = deviations(windows(field, world, cells, 50.0, 21, threads));
    std::vector<double> five;
    for (const ReliefGrid& w : windows(field, world, cells, 500.0, 11, threads)) {
        five.push_back(relief(w));
    }
    f.plainRelief5k = percentile(five, 0.5);
    std::vector<double> reliefs;
    for (const ReliefGrid& w : windows(field, world, cells, 1000.0, 11, threads)) {
        reliefs.push_back(relief(w));
    }
    f.plainWindows = static_cast<int>(reliefs.size());
    f.plainReliefMin = percentile(reliefs, 0.0);
    f.plainRelief10 = percentile(reliefs, 0.1);
    f.plainRelief50 = percentile(reliefs, 0.5);
    f.plainRelief90 = percentile(reliefs, 0.9);
    const auto flat = std::count_if(reliefs.begin(), reliefs.end(),
                                    [](double r) { return r < kFlatRelief; });
    f.plainFlatShare =
        reliefs.empty() ? 0.0 : static_cast<double>(flat) / static_cast<double>(reliefs.size());
}

// Mean windows: 41 x 41 samples 100 m apart; the 21 x 21 inner points, each against the mean of
// the 21 x 21 samples around it.
constexpr int kMeanSide = 41;
constexpr int kMeanHalf = 10;

// Points below their local mean and points counted, over the windows.
struct BelowCount {
    double below = 0.0;
    double points = 0.0;

    [[nodiscard]] double share() const { return points > 0.0 ? below / points : 0.0; }
};

void addBelow(const ReliefGrid& w, BelowCount& count) {
    // Summed areas of the heights and of the sea samples, (side + 1)^2 with a zero border.
    const int n = w.columns + 1;
    std::vector<double> sum(static_cast<std::size_t>(n) * static_cast<std::size_t>(n), 0.0);
    std::vector<int> sea(sum.size(), 0);
    const auto at = [n](int i, int j) {
        return static_cast<std::size_t>(j) * static_cast<std::size_t>(n) +
               static_cast<std::size_t>(i);
    };
    for (int j = 0; j < w.rows; ++j) {
        for (int i = 0; i < w.columns; ++i) {
            const float h = w.at(i, j);
            sum[at(i + 1, j + 1)] = h + sum[at(i, j + 1)] + sum[at(i + 1, j)] - sum[at(i, j)];
            sea[at(i + 1, j + 1)] = (h < kSeaLevel ? 1 : 0) + sea[at(i, j + 1)] +
                                    sea[at(i + 1, j)] - sea[at(i, j)];
        }
    }
    const double area = (2 * kMeanHalf + 1) * (2 * kMeanHalf + 1);
    for (int j = kMeanHalf; j + kMeanHalf < w.rows; ++j) {
        for (int i = kMeanHalf; i + kMeanHalf < w.columns; ++i) {
            const int i0 = i - kMeanHalf;
            const int j0 = j - kMeanHalf;
            const int i1 = i + kMeanHalf + 1;
            const int j1 = j + kMeanHalf + 1;
            if (sea[at(i1, j1)] - sea[at(i0, j1)] - sea[at(i1, j0)] + sea[at(i0, j0)] > 0) {
                continue;
            }
            const double mean =
                (sum[at(i1, j1)] - sum[at(i0, j1)] - sum[at(i1, j0)] + sum[at(i0, j0)]) / area;
            count.below += w.at(i, j) < mean ? 1.0 : 0.0;
            count.points += 1.0;
        }
    }
}

double belowShare(const Heightfield& field, const ReliefGrid& world,
                  const std::vector<std::size_t>& cells, int count, int threads, int* used) {
    BelowCount below;
    const std::vector<std::size_t> picked = spread(cells, count);
    for (const ReliefGrid& w : windows(field, world, picked, 100.0, kMeanSide, threads)) {
        addBelow(w, below);
    }
    if (used != nullptr) {
        *used = static_cast<int>(picked.size());
    }
    return below.share();
}

// max - min over 10 km around every land cell of the grid (6 x 6 samples 2 km apart), -1 at sea.
std::vector<float> cellReliefs(const Heightfield& field, const ReliefGrid& world,
                               const ReliefStats& stats, int threads) {
    constexpr int kSamples = 6;
    constexpr double kStep = 2000.0;
    std::vector<float> out(stats.cells.size(), -1.0f);
    planet::parallelRows(world.rows, planet::resolveThreads(threads), [&](int begin, int end) {
        std::vector<double> xs;
        std::vector<double> zs;
        std::vector<float> heights;
        for (int j = begin; j < end; ++j) {
            xs.clear();
            zs.clear();
            for (int i = 0; i < world.columns; ++i) {
                if ((stats.cells[world.index(i, j)] & kReliefLand) == 0U) {
                    continue;
                }
                for (int b = 0; b < kSamples; ++b) {
                    for (int a = 0; a < kSamples; ++a) {
                        xs.push_back(world.x(i) + (a - 0.5 * (kSamples - 1)) * kStep);
                        zs.push_back(world.z(j) + (b - 0.5 * (kSamples - 1)) * kStep);
                    }
                }
            }
            heights.resize(xs.size());
            field.heightsAt(xs, zs, heights);
            std::size_t k = 0;
            for (int i = 0; i < world.columns; ++i) {
                if ((stats.cells[world.index(i, j)] & kReliefLand) == 0U) {
                    continue;
                }
                const auto first = heights.begin() + static_cast<std::ptrdiff_t>(k);
                const auto [low, high] = std::minmax_element(first, first + kSamples * kSamples);
                out[world.index(i, j)] = *high - *low;
                k += kSamples * kSamples;
            }
        }
    });
    return out;
}

// The widest zone of member cells: a two-pass chamfer distance to the nearest other cell (the
// edges of the grid count as such), then the area of the 8-connected zone around its maximum.
ReliefZone widestZone(const ReliefGrid& world, const std::vector<std::uint8_t>& member) {
    const double diagonal = std::sqrt(2.0);
    std::vector<double> d(member.size(), 0.0);
    const auto relax = [&](int i, int j, int di, int dj, double w) {
        const int a = i + di;
        const int b = j + dj;
        const double from = a < 0 || b < 0 || a >= world.columns || b >= world.rows
                                ? 0.0
                                : d[world.index(a, b)];
        d[world.index(i, j)] = std::min(d[world.index(i, j)], from + w);
    };
    for (int j = 0; j < world.rows; ++j) {
        for (int i = 0; i < world.columns; ++i) {
            if (member[world.index(i, j)] == 0U) {
                continue;
            }
            d[world.index(i, j)] = std::numeric_limits<double>::max();
            relax(i, j, -1, 0, 1.0);
            relax(i, j, 0, -1, 1.0);
            relax(i, j, -1, -1, diagonal);
            relax(i, j, 1, -1, diagonal);
        }
    }
    for (int j = world.rows - 1; j >= 0; --j) {
        for (int i = world.columns - 1; i >= 0; --i) {
            if (member[world.index(i, j)] != 0U) {
                relax(i, j, 1, 0, 1.0);
                relax(i, j, 0, 1, 1.0);
                relax(i, j, 1, 1, diagonal);
                relax(i, j, -1, 1, diagonal);
            }
        }
    }
    const auto widest = std::max_element(d.begin(), d.end());
    ReliefZone zone;
    if (widest == d.end() || *widest <= 0.0) {
        return zone;
    }
    const auto cell = static_cast<std::size_t>(widest - d.begin());
    const int ci = static_cast<int>(cell % static_cast<std::size_t>(world.columns));
    const int cj = static_cast<int>(cell / static_cast<std::size_t>(world.columns));
    zone.width = 2.0 * (*widest - 0.5) * world.spacing;
    zone.x = world.x(ci);
    zone.z = world.z(cj);
    std::vector<std::uint8_t> seen(member.size(), 0);
    std::vector<std::size_t> queue{cell};
    seen[cell] = 1;
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const int i = static_cast<int>(queue[head] % static_cast<std::size_t>(world.columns));
        const int j = static_cast<int>(queue[head] / static_cast<std::size_t>(world.columns));
        for (int dj = -1; dj <= 1; ++dj) {
            for (int di = -1; di <= 1; ++di) {
                const int a = i + di;
                const int b = j + dj;
                if (a >= 0 && b >= 0 && a < world.columns && b < world.rows &&
                    member[world.index(a, b)] != 0U && seen[world.index(a, b)] == 0U) {
                    seen[world.index(a, b)] = 1;
                    queue.push_back(world.index(a, b));
                }
            }
        }
    }
    zone.area = static_cast<double>(queue.size()) * world.spacing * world.spacing;
    return zone;
}

void measureZones(const Heightfield& field, const ReliefGrid& world, const ReliefStats& stats,
                  const std::vector<std::size_t>& belts, int threads, TerrainFigures& f) {
    const std::vector<float> reliefs = cellReliefs(field, world, stats, threads);
    std::vector<std::uint8_t> highland(reliefs.size(), 0);
    for (const std::size_t cell : belts) {
        highland[cell] = 1;
    }
    std::vector<std::uint8_t> flat(reliefs.size(), 0);
    std::vector<std::uint8_t> dull(reliefs.size(), 0);
    double land = 0.0;
    double flats = 0.0;
    double highlands = 0.0;
    double dulls = 0.0;
    for (std::size_t cell = 0; cell < reliefs.size(); ++cell) {
        if (reliefs[cell] < 0.0f) {
            continue;
        }
        highland[cell] = highland[cell] != 0U || world.heights[cell] >= kHighland ? 1 : 0;
        flat[cell] = reliefs[cell] < kFlatRelief ? 1 : 0;
        dull[cell] = highland[cell] != 0U && reliefs[cell] < kDullRelief ? 1 : 0;
        land += 1.0;
        flats += flat[cell];
        highlands += highland[cell];
        dulls += dull[cell];
    }
    f.flatShare = land > 0.0 ? flats / land : 0.0;
    f.dullShare = highlands > 0.0 ? dulls / highlands : 0.0;
    f.widestFlat = widestZone(world, flat);
    f.widestDull = widestZone(world, dull);
}

// The shares of the land cells by class of intensity (none without the geography).
std::array<double, 5> reliefShares(const Heightfield& field, const ReliefGrid& world,
                                   const ReliefStats& stats, int threads) {
    std::array<double, 5> shares{};
    if (field.geography() == nullptr) {
        return shares;
    }
    std::vector<std::uint8_t> classOf(stats.cells.size(), 255);
    planet::parallelRows(world.rows, planet::resolveThreads(threads), [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < world.columns; ++i) {
                if ((stats.cells[world.index(i, j)] & kReliefLand) == 0U) {
                    continue;
                }
                const float relief = field.sampleAt(world.x(i), world.z(j)).relief;
                std::uint8_t c = 0;
                while (c < kReliefClasses.size() && relief >= kReliefClasses[c]) {
                    ++c;
                }
                classOf[world.index(i, j)] = c;
            }
        }
    });
    double land = 0.0;
    for (const std::uint8_t c : classOf) {
        if (c != 255) {
            shares[c] += 1.0;
            land += 1.0;
        }
    }
    for (double& share : shares) {
        share = land > 0.0 ? share / land : 0.0;
    }
    return shares;
}

} // namespace

TerrainFigures measureTerrain(const Heightfield& field, const ReliefGrid& world,
                              const ReliefStats& stats, int windowCount, int threads) {
    TerrainFigures f;
    const std::vector<std::size_t> belts = beltsOf(field, world, stats);
    measureBelts(field, world, belts, windowCount, threads, f);
    measurePlains(field, world, stats, 4 * windowCount, threads, f);
    f.belowMean = belowShare(field, world, cellsWith(stats, kReliefLand), 4 * windowCount,
                             threads, &f.meanWindows);
    f.belowMeanPlains =
        belowShare(field, world, cellsWith(stats, kReliefPlain), windowCount, threads, nullptr);
    f.belowMeanBelts = belowShare(field, world, belts, windowCount, threads, nullptr);
    measureZones(field, world, stats, belts, threads, f);
    f.reliefShares = reliefShares(field, world, stats, threads);
    return f;
}

} // namespace terrain

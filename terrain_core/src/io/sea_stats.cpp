#include "terrain/io/sea_stats.hpp"

#include "../generation/planet/planet_parallel.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/io/relief_png.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace terrain {
namespace {

constexpr double kDegrees = 57.29577951308232;
constexpr double kInfinity = std::numeric_limits<double>::infinity();

double depthOf(float height) {
    return height < kSeaLevel ? static_cast<double>(kSeaLevel - height) : 0.0;
}

std::size_t bandOf(double depth) {
    std::size_t band = 0;
    while (band < kSeaDepthBands.size() && !(depth < static_cast<double>(kSeaDepthBands[band]))) {
        ++band;
    }
    return band;
}

// The distance of every cell to the nearest land cell, meters, over the 8 neighbors on the torus
// of the grid (Dijkstra: the same distances whatever the order of the ties).
std::vector<double> landDistances(const ReliefGrid& grid) {
    std::vector<double> distance(grid.heights.size(), kInfinity);
    using Entry = std::pair<double, std::size_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
    for (int j = 0; j < grid.rows; ++j) {
        for (int i = 0; i < grid.columns; ++i) {
            if (!grid.sea(i, j)) {
                distance[grid.index(i, j)] = 0.0;
                queue.push({0.0, grid.index(i, j)});
            }
        }
    }
    const double diagonal = std::sqrt(2.0) * grid.spacing;
    while (!queue.empty()) {
        const auto [d, at] = queue.top();
        queue.pop();
        if (d > distance[at]) {
            continue;
        }
        const int i = static_cast<int>(at % static_cast<std::size_t>(grid.columns));
        const int j = static_cast<int>(at / static_cast<std::size_t>(grid.columns));
        for (int dj = -1; dj <= 1; ++dj) {
            for (int di = -1; di <= 1; ++di) {
                if (di == 0 && dj == 0) {
                    continue;
                }
                const int ni = (i + di + grid.columns) % grid.columns;
                const int nj = (j + dj + grid.rows) % grid.rows;
                const std::size_t next = grid.index(ni, nj);
                const double step = di != 0 && dj != 0 ? diagonal : grid.spacing;
                if (d + step < distance[next]) {
                    distance[next] = d + step;
                    queue.push({d + step, next});
                }
            }
        }
    }
    return distance;
}

double distanceAt(const ReliefGrid& grid, const std::vector<double>& distance, int i, int j) {
    return distance[grid.index((i + grid.columns) % grid.columns, (j + grid.rows) % grid.rows)];
}

bool nextToLand(const ReliefGrid& grid, int i, int j) {
    const int left = (i + grid.columns - 1) % grid.columns;
    const int right = (i + 1) % grid.columns;
    const int up = (j + grid.rows - 1) % grid.rows;
    const int down = (j + 1) % grid.rows;
    return !grid.sea(left, j) || !grid.sea(right, j) || !grid.sea(i, up) || !grid.sea(i, down);
}

struct Start {
    double x = 0.0;
    double z = 0.0;
    double dirX = 0.0;
    double dirZ = 0.0;
};

// The cells of the sea next to the land, every so many of them, with the direction away from
// the land (down the gradient of the distance to it).
std::vector<Start> transectStarts(const ReliefGrid& grid, const std::vector<double>& distance,
                                  int most) {
    std::vector<Start> all;
    for (int j = 0; j < grid.rows; ++j) {
        for (int i = 0; i < grid.columns; ++i) {
            if (!grid.sea(i, j) || !nextToLand(grid, i, j)) {
                continue;
            }
            const double gx =
                distanceAt(grid, distance, i + 1, j) - distanceAt(grid, distance, i - 1, j);
            const double gz =
                distanceAt(grid, distance, i, j + 1) - distanceAt(grid, distance, i, j - 1);
            const double length = dmath::hypot(gx, gz);
            if (length > 0.0 && std::isfinite(length)) {
                all.push_back({grid.x(i), grid.z(j), gx / length, gz / length});
            }
        }
    }
    std::vector<Start> starts;
    if (all.empty() || most <= 0) {
        return starts;
    }
    const std::size_t stride =
        std::max<std::size_t>(1, all.size() / static_cast<std::size_t>(most));
    for (std::size_t k = stride / 2;
         k < all.size() && starts.size() < static_cast<std::size_t>(most); k += stride) {
        starts.push_back(all[k]);
    }
    return starts;
}

// The depths along a profile, up to the first land after its start.
std::vector<double> profileDepths(const Heightfield& field, const Start& s,
                                  const SeaStatsParams& p) {
    const WorldSize& world = field.world();
    const auto count = static_cast<std::size_t>(p.transectLength / p.transectStep) + 1;
    std::vector<double> xs(count);
    std::vector<double> zs(count);
    for (std::size_t k = 0; k < count; ++k) {
        const double along = static_cast<double>(k) * p.transectStep;
        xs[k] = noise::wrapCanonical(s.x + s.dirX * along, world.x);
        zs[k] = noise::wrapCanonical(s.z + s.dirZ * along, world.z);
    }
    std::vector<HeightSample> samples(count);
    field.samplesAt(xs, zs, samples);
    std::vector<double> depths;
    depths.reserve(count);
    for (std::size_t k = 0; k < count; ++k) {
        if (k > 0 && samples[k].water != WaterKind::Ocean) {
            break;
        }
        depths.push_back(depthOf(samples[k].height));
    }
    return depths;
}

// The first index from `from` at which the depth reaches `depth`, or depths.size().
std::size_t firstAtDepth(const std::vector<double>& depths, std::size_t from, double depth) {
    std::size_t k = from;
    while (k < depths.size() && depths[k] < depth) {
        ++k;
    }
    return k;
}

double degreesOf(double drop, double run) {
    return run > 0.0 ? dmath::atan(drop / run) * kDegrees : 90.0;
}

SeaTransect measureTransect(const std::vector<double>& depths, const Start& s,
                            const SeaStatsParams& p) {
    SeaTransect t;
    t.x = s.x;
    t.z = s.z;
    const double heading = dmath::atan2(s.dirX, -s.dirZ) * kDegrees;
    t.heading = heading < 0.0 ? heading + 360.0 : heading;
    t.length = static_cast<double>(depths.size() - 1) * p.transectStep;
    const auto window =
        std::max<std::size_t>(1, static_cast<std::size_t>(p.breakWindow / p.transectStep));
    for (std::size_t k = 0; k + window < depths.size(); ++k) {
        const double gradient =
            (depths[k + window] - depths[k]) / (static_cast<double>(window) * p.transectStep);
        if (gradient > p.breakGradient) {
            t.shelfBreak = true;
            t.breakDepth = depths[k];
            t.breakDistance = static_cast<double>(k) * p.transectStep;
            const std::size_t end = firstAtDepth(depths, k, depths[k] + p.slopeDrop);
            if (end < depths.size()) {
                t.slope = true;
                t.slopeDegrees = degreesOf(depths[end] - depths[k],
                                           static_cast<double>(end - k) * p.transectStep);
            }
            break;
        }
    }
    const std::size_t top = firstAtDepth(depths, 0, p.riseTop);
    const std::size_t bottom = firstAtDepth(depths, top, p.riseBottom);
    if (bottom < depths.size()) {
        t.rise = true;
        t.riseDegrees = degreesOf(depths[bottom] - depths[top],
                                  static_cast<double>(bottom - top) * p.transectStep);
    }
    const auto reach =
        std::min(depths.size(), static_cast<std::size_t>(p.trenchReach / p.transectStep) + 1);
    for (std::size_t k = 0; k < reach; ++k) {
        if (depths[k] > t.deepest) {
            t.deepest = depths[k];
            t.deepestDistance = static_cast<double>(k) * p.transectStep;
        }
    }
    return t;
}

// The rms of the floor about its best plane in a window centered on (x, z), or a negative value
// when the window is not all sea.
double windowRms(const Heightfield& field, double x, double z, const SeaStatsParams& p) {
    const auto n = static_cast<std::size_t>(p.hillSamples);
    const double half = 0.5 * static_cast<double>(n - 1);
    std::vector<double> xs(n);
    std::vector<double> zs(n);
    std::vector<HeightSample> row(n);
    std::vector<double> heights;
    heights.reserve(n * n);
    for (std::size_t j = 0; j < n; ++j) {
        for (std::size_t i = 0; i < n; ++i) {
            xs[i] = noise::wrapCanonical(x + (static_cast<double>(i) - half) * p.hillSpacing,
                                         field.world().x);
            zs[i] = noise::wrapCanonical(z + (static_cast<double>(j) - half) * p.hillSpacing,
                                         field.world().z);
        }
        field.samplesAt(xs, zs, row);
        for (const HeightSample& s : row) {
            if (s.water != WaterKind::Ocean) {
                return -1.0;
            }
            heights.push_back(static_cast<double>(s.height));
        }
    }
    // The grid is centered: the plane is the mean plus its two slopes.
    double mean = 0.0;
    double sx = 0.0;
    double sz = 0.0;
    double xx = 0.0;
    for (std::size_t j = 0; j < n; ++j) {
        for (std::size_t i = 0; i < n; ++i) {
            const double h = heights[j * n + i];
            const double u = static_cast<double>(i) - half;
            const double v = static_cast<double>(j) - half;
            mean += h;
            sx += h * u;
            sz += h * v;
            xx += u * u;
        }
    }
    mean /= static_cast<double>(n * n);
    double squares = 0.0;
    for (std::size_t j = 0; j < n; ++j) {
        for (std::size_t i = 0; i < n; ++i) {
            const double u = static_cast<double>(i) - half;
            const double v = static_cast<double>(j) - half;
            const double residual = heights[j * n + i] - mean - (sx / xx) * u - (sz / xx) * v;
            squares += residual * residual;
        }
    }
    return std::sqrt(squares / static_cast<double>(n * n));
}

void addHypsometry(const ReliefGrid& grid, const std::vector<double>& distance,
                   const SeaStatsParams& p, SeaStats& out) {
    std::vector<double> depths;
    std::vector<double> open;
    double sum = 0.0;
    for (int j = 0; j < grid.rows; ++j) {
        for (int i = 0; i < grid.columns; ++i) {
            ++out.cells;
            if (!grid.sea(i, j)) {
                continue;
            }
            const double depth = depthOf(grid.at(i, j));
            ++out.seaCells;
            out.bands[bandOf(depth)] += 1.0;
            sum += depth;
            depths.push_back(depth);
            if (depth > out.deepest) {
                out.deepest = depth;
                out.deepestX = grid.x(i);
                out.deepestZ = grid.z(j);
            }
            if (distance[grid.index(i, j)] >= p.openDistance) {
                open.push_back(depth);
            }
        }
    }
    if (out.seaCells == 0) {
        return;
    }
    for (double& band : out.bands) {
        band /= static_cast<double>(out.seaCells);
    }
    out.meanDepth = sum / static_cast<double>(out.seaCells);
    out.medianDepth = spreadOf(depths).median;
    out.openCells = open.size();
    if (!open.empty()) {
        std::sort(open.begin(), open.end());
        out.openMedianDepth = open[open.size() / 2];
        out.openShallowDepth = open[open.size() / 20];
    }
}

void addTransects(const Heightfield& field, const ReliefGrid& grid,
                  const std::vector<double>& distance, const SeaStatsParams& p, SeaStats& out) {
    const std::vector<Start> starts = transectStarts(grid, distance, p.transects);
    std::vector<SeaTransect> measured(starts.size());
    std::vector<char> kept(starts.size(), 0);
    planet::parallelBlocks(
        static_cast<int>(starts.size()), 1, planet::resolveThreads(p.threads),
        [&](int begin, int end) {
            for (int k = begin; k < end; ++k) {
                const auto at = static_cast<std::size_t>(k);
                const std::vector<double> depths = profileDepths(field, starts[at], p);
                if (static_cast<double>(depths.size() - 1) * p.transectStep >= p.minLength) {
                    measured[at] = measureTransect(depths, starts[at], p);
                    kept[at] = 1;
                }
            }
        });
    std::vector<double> breakDepth, breakDistance, slope, rise, trench;
    for (std::size_t k = 0; k < measured.size(); ++k) {
        if (kept[k] == 0) {
            continue;
        }
        const SeaTransect& t = measured[k];
        out.transects.push_back(t);
        if (t.shelfBreak) {
            breakDepth.push_back(t.breakDepth);
            breakDistance.push_back(t.breakDistance);
        }
        if (t.slope) {
            slope.push_back(t.slopeDegrees);
        }
        if (t.rise) {
            rise.push_back(t.riseDegrees);
        }
        trench.push_back(t.deepest);
    }
    out.breakDepth = spreadOf(breakDepth);
    out.breakDistance = spreadOf(breakDistance);
    out.slopeDegrees = spreadOf(slope);
    out.riseDegrees = spreadOf(rise);
    out.trenchDepth = spreadOf(trench);
}

void addHills(const Heightfield& field, const ReliefGrid& grid, const std::vector<double>& distance,
              const SeaStatsParams& p, SeaStats& out) {
    std::vector<std::pair<double, double>> centers;
    for (int j = 0; j < grid.rows; ++j) {
        for (int i = 0; i < grid.columns; ++i) {
            if (grid.sea(i, j) && distance[grid.index(i, j)] >= p.openDistance &&
                depthOf(grid.at(i, j)) >= p.hillDepth) {
                centers.emplace_back(grid.x(i), grid.z(j));
            }
        }
    }
    if (centers.empty() || p.hillWindows <= 0) {
        return;
    }
    const std::size_t stride =
        std::max<std::size_t>(1, centers.size() / static_cast<std::size_t>(p.hillWindows));
    std::vector<std::pair<double, double>> chosen;
    for (std::size_t k = stride / 2;
         k < centers.size() && chosen.size() < static_cast<std::size_t>(p.hillWindows);
         k += stride) {
        chosen.push_back(centers[k]);
    }
    std::vector<double> rms(chosen.size(), -1.0);
    planet::parallelBlocks(static_cast<int>(chosen.size()), 1, planet::resolveThreads(p.threads),
                           [&](int begin, int end) {
                               for (int k = begin; k < end; ++k) {
                                   const auto at = static_cast<std::size_t>(k);
                                   rms[at] =
                                       windowRms(field, chosen[at].first, chosen[at].second, p);
                               }
                           });
    std::vector<double> kept;
    for (double value : rms) {
        if (!(value < 0.0)) {
            kept.push_back(value);
        }
    }
    const SeaSpread spread = spreadOf(kept);
    out.hillWindows = spread.count;
    out.hillRms = spread.median;
    out.hillRmsP90 = spread.p90;
}

} // namespace

SeaSpread spreadOf(std::vector<double> values) {
    SeaSpread s;
    s.count = values.size();
    if (values.empty()) {
        return s;
    }
    std::sort(values.begin(), values.end());
    const auto at = [&](double q) {
        return values[std::min(values.size() - 1,
                               static_cast<std::size_t>(q * static_cast<double>(values.size())))];
    };
    s.p10 = at(0.1);
    s.median = at(0.5);
    s.p90 = at(0.9);
    return s;
}

SeaStats measureSeas(const Heightfield& field, const SeaStatsParams& params) {
    const ReliefGrid grid = sampleReliefWorld(field, params.cellSize, params.threads);
    const std::vector<double> distance = landDistances(grid);
    SeaStats out;
    addHypsometry(grid, distance, params, out);
    addTransects(field, grid, distance, params, out);
    addHills(field, grid, distance, params, out);
    return out;
}

} // namespace terrain

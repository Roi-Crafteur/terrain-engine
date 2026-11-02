// The field of the ergs on blocks of the planet level (ergs_impl.hpp): the potential of the
// climate of each block (dry, warm, far from the sea, on land), blurred along the meridians, and
// the water cells the dunes keep away from.
#include "ergs_impl.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/planet/planet_climate.hpp"
#include "terrain/generation/planet/planet_level.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

namespace terrain::erg_field {

namespace {

// The humidity reads the potential evapotranspiration as kLeastPet mm/yr at least, as
// PlanetLevel::humidityAt.
constexpr double kLeastPet = 300.0;
// The land of a block: none below kLandLow of its cells, all above kLandHigh. A block of the
// sea at kOceanShare of its cells or more is a source of the distance to the sea.
constexpr float kLandLow = 0.5f;
constexpr float kLandHigh = 0.9f;
constexpr double kOceanShare = 0.5;
// The climate is blurred across the meridians over kAcross blocks (two passes).
constexpr int kAcross = 2;
constexpr int kLocal = 2;

int blockCellsOf(const PlanetGrid& grid) {
    int b = kMostBlockCells;
    while (b > 1 && (grid.nx % b != 0 || grid.nz % b != 0)) {
        b /= 2;
    }
    return b;
}

struct Sums {
    std::vector<float> land;        // land cells
    std::vector<float> climate;     // sum over the land cells of their climate factor
    std::vector<float> humidity;    // and of their humidity
    std::vector<float> temperature; // and of their temperature
    std::vector<float> ocean;       // ocean cells
};

double humidityOf(const PlanetLevel& planet, std::size_t c) {
    const double p = planet.precipitation(c);
    const double pet = std::max(
        planet::potentialEvapotranspiration(planet.temperature(c), planet.params().climate),
        kLeastPet);
    return p / (p + pet);
}

// 0: no water, 1: a lake, 2: a river carved.
int waterOf(const PlanetLevel& planet, std::size_t c, double minDischarge) {
    const PlanetCells& cells = planet.cells();
    if ((cells.bits[c] & (PlanetCells::kLakeBit | PlanetCells::kRiverBit)) == 0) {
        return 0;
    }
    if (cells.lakeAt(c) >= 0) {
        return 1;
    }
    const PlanetCells::RiverCell* river = cells.riverAt(c);
    if (river == nullptr) {
        return 0;
    }
    const RiverSegment& segment =
        planet.rivers().segments[static_cast<std::size_t>(river->segment)];
    const bool carved = static_cast<double>(
                            segment.vertices[static_cast<std::size_t>(river->vertex)].discharge) >=
                        minDischarge;
    return carved ? 2 : 0;
}

// The receivers of the river cells, block by block in the order of their bits.
void indexRivers(const PlanetLevel& planet, Field& f) {
    f.riverStart.assign(f.water.size() + 1, 0);
    for (int bj = 0; bj < f.bz; ++bj) {
        for (int bi = 0; bi < f.bx; ++bi) {
            const std::size_t b = f.block(bi, bj);
            f.riverStart[b] = static_cast<std::uint32_t>(f.receivers.size());
            const std::uint64_t rivers = f.water[b] & ~f.lakes[b];
            for (int bit = 0; bit < 64; ++bit) {
                if (((rivers >> bit) & 1u) == 0) {
                    continue;
                }
                const int i = bi * f.blockCells + bit % 8;
                const int j = bj * f.blockCells + bit / 8;
                f.receivers.push_back(planet.cells().receiver(f.grid.index(i, j)));
            }
        }
    }
    f.riverStart.back() = static_cast<std::uint32_t>(f.receivers.size());
}

// The climate factor of a land cell: dry and warm.
float cellClimate(const PlanetLevel& planet, std::size_t c, const ErgParams& params) {
    const double t = planet.temperature(c);
    const auto humidity = static_cast<float>(humidityOf(planet, c));
    return (1.0f - noise::smoothstep(params.aridHumidity, params.dryHumidity, humidity)) *
           noise::smoothstep(params.coldTemperature, params.warmTemperature,
                             static_cast<float>(t));
}

Sums sumBlocks(const PlanetLevel& planet, const ErgParams& params, Field& f) {
    Sums s;
    const std::size_t blocks = static_cast<std::size_t>(f.bx) * static_cast<std::size_t>(f.bz);
    s.land.assign(blocks, 0.0f);
    s.climate.assign(blocks, 0.0f);
    s.humidity.assign(blocks, 0.0f);
    s.temperature.assign(blocks, 0.0f);
    s.ocean.assign(blocks, 0.0f);
    f.water.assign(blocks, 0);
    f.lakes.assign(blocks, 0);
    const PlanetGrid& g = f.grid;
    for (int j = 0; j < g.nz; ++j) {
        for (int i = 0; i < g.nx; ++i) {
            const std::size_t c = g.index(i, j);
            const std::size_t b = f.block(i / f.blockCells, j / f.blockCells);
            if (planet.cells().ocean(c)) {
                s.ocean[b] += 1.0f;
                continue;
            }
            s.land[b] += 1.0f;
            s.climate[b] += cellClimate(planet, c, params);
            s.humidity[b] += static_cast<float>(humidityOf(planet, c));
            s.temperature[b] += planet.temperature(c);
            const int water = waterOf(planet, c, params.minDischarge);
            if (water != 0) {
                const std::uint64_t bit = std::uint64_t{1} << (i % f.blockCells + 8 * (j % f.blockCells));
                f.water[b] |= bit;
                f.lakes[b] |= water == 1 ? bit : 0;
            }
        }
    }
    return s;
}

// A box pass of radius r over the blocks, along x (alongX) or z, on the torus.
std::vector<float> boxPass(const Field& f, const std::vector<float>& in, int r, bool alongX) {
    if (r <= 0) {
        return in;
    }
    std::vector<float> out(in.size(), 0.0f);
    const int n = alongX ? f.bx : f.bz;
    const int lines = alongX ? f.bz : f.bx;
    for (int line = 0; line < lines; ++line) {
        const auto at = [&](int k) {
            const int w = ((k % n) + n) % n;
            return alongX ? f.block(w, line) : f.block(line, w);
        };
        double sum = 0.0;
        for (int k = -r; k <= r; ++k) {
            sum += static_cast<double>(in[at(k)]);
        }
        for (int k = 0; k < n; ++k) {
            out[at(k)] = static_cast<float>(sum / static_cast<double>(2 * r + 1));
            sum += static_cast<double>(in[at(k + r + 1)]) - static_cast<double>(in[at(k - r)]);
        }
    }
    return out;
}

// The climate of the blocks weighted by their land, blurred: two passes along the meridians,
// two across them.
std::vector<float> blurredClimate(const Field& f, const Sums& s, int radius) {
    std::vector<float> num = s.climate;
    std::vector<float> den = s.land;
    for (int pass = 0; pass < 2; ++pass) {
        num = boxPass(f, num, radius, false);
        den = boxPass(f, den, radius, false);
    }
    for (int pass = 0; pass < 2; ++pass) {
        num = boxPass(f, num, kAcross, true);
        den = boxPass(f, den, kAcross, true);
    }
    std::vector<float> out(num.size(), 0.0f);
    for (std::size_t b = 0; b < out.size(); ++b) {
        out[b] = den[b] > 0.0f ? std::clamp(num[b] / den[b], 0.0f, 1.0f) : 0.0f;
    }
    return out;
}

// The mean of a sum over the land of the blocks, blurred over kLocal blocks on each side (the land
// around a point, without the sharp lines between the cells of the planet level).
std::vector<float> localMean(const Field& f, const std::vector<float>& sum, const Sums& s) {
    std::vector<float> num = boxPass(f, boxPass(f, sum, kLocal, true), kLocal, false);
    const std::vector<float> den = boxPass(f, boxPass(f, s.land, kLocal, true), kLocal, false);
    for (std::size_t b = 0; b < num.size(); ++b) {
        num[b] = den[b] > 0.0f ? num[b] / den[b] : 0.0f;
    }
    return num;
}

// Meters from every block to the nearest block of the sea (Dijkstra over the 8 neighbors), or
// infinity without any.
std::vector<double> seaDistance(const Field& f, const Sums& s) {
    const double cells = static_cast<double>(f.blockCells * f.blockCells);
    std::vector<double> d(s.ocean.size(), std::numeric_limits<double>::infinity());
    using Item = std::pair<double, std::size_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> queue;
    for (std::size_t b = 0; b < d.size(); ++b) {
        if (static_cast<double>(s.ocean[b]) >= kOceanShare * cells) {
            d[b] = 0.0;
            queue.emplace(0.0, b);
        }
    }
    while (!queue.empty()) {
        const auto [dist, b] = queue.top();
        queue.pop();
        if (dist > d[b]) {
            continue;
        }
        const int bi = static_cast<int>(b % static_cast<std::size_t>(f.bx));
        const int bj = static_cast<int>(b / static_cast<std::size_t>(f.bx));
        for (const PlanetNeighbor& n : kPlanetNeighbors) {
            const std::size_t to = f.block(((bi + n.dx) % f.bx + f.bx) % f.bx,
                                           ((bj + n.dz) % f.bz + f.bz) % f.bz);
            const double step = f.blockSize * (n.diagonal ? kPlanetDiagonal : 1.0);
            if (dist + step < d[to]) {
                d[to] = dist + step;
                queue.emplace(d[to], to);
            }
        }
    }
    return d;
}

// The blocks within r blocks (Chebyshev) of a block where on holds.
std::vector<std::uint8_t> dilated(const Field& f, int r,
                                  const std::function<bool(std::size_t)>& on) {
    std::vector<std::uint8_t> along(f.potential.size(), 0);
    std::vector<std::uint8_t> out(f.potential.size(), 0);
    const auto spread = [&](std::vector<std::uint8_t>& to, bool alongX,
                            const std::function<bool(std::size_t)>& source) {
        const int n = alongX ? f.bx : f.bz;
        const int lines = alongX ? f.bz : f.bx;
        for (int line = 0; line < lines; ++line) {
            for (int k = 0; k < n; ++k) {
                const std::size_t b = alongX ? f.block(k, line) : f.block(line, k);
                if (!source(b)) {
                    continue;
                }
                for (int o = -std::min(r, n); o <= std::min(r, n); ++o) {
                    const int w = ((k + o) % n + n) % n;
                    to[alongX ? f.block(w, line) : f.block(line, w)] = 1;
                }
            }
        }
    };
    spread(along, true, on);
    spread(out, false, [&](std::size_t b) { return along[b] != 0; });
    return out;
}

// The distance from (px, pz) (from the center of a river cell) to its course: the segment from
// that center to the center of its receiver (the center alone without one).
double segmentDistance(double px, double pz, std::uint8_t receiver, double cs) {
    if (receiver >= kPlanetNeighbors.size()) {
        return dmath::hypot(px, pz);
    }
    const double ex = kPlanetNeighbors[receiver].dx * cs;
    const double ez = kPlanetNeighbors[receiver].dz * cs;
    const double t = std::clamp((px * ex + pz * ez) / (ex * ex + ez * ez), 0.0, 1.0);
    return dmath::hypot(px - t * ex, pz - t * ez);
}

} // namespace

Field::Water Field::waterCell(int i, int j) const {
    i = grid.wrapColumn(i);
    j = grid.wrapRow(j);
    const std::size_t b = block(i / blockCells, j / blockCells);
    const int bit = i % blockCells + 8 * (j % blockCells);
    Water w;
    w.any = ((water[b] >> bit) & 1u) != 0;
    if (!w.any) {
        return w;
    }
    w.lake = ((lakes[b] >> bit) & 1u) != 0;
    if (!w.lake) {
        const std::uint64_t rivers = water[b] & ~lakes[b];
        const std::uint64_t below = bit == 0 ? 0 : (rivers & ((std::uint64_t{1} << bit) - 1));
        w.receiver = receivers[riverStart[b] + static_cast<std::uint32_t>(std::popcount(below))];
    }
    return w;
}

Field build(const PlanetLevel& planet, const ErgParams& params, double warpReach) {
    Field f;
    f.grid = planet.grid();
    f.blockCells = blockCellsOf(f.grid);
    f.bx = f.grid.nx / f.blockCells;
    f.bz = f.grid.nz / f.blockCells;
    f.blockSize = f.grid.cellSize * f.blockCells;
    const Sums sums = sumBlocks(planet, params, f);
    const double degree = f.grid.periodZ() / 180.0;
    const auto radius = static_cast<int>(std::floor(params.blurDegrees * degree / f.blockSize + 0.5));
    const std::vector<float> climate = blurredClimate(f, sums, radius);
    const std::vector<double> sea = seaDistance(f, sums);
    f.humidity = localMean(f, sums.humidity, sums);
    f.temperature = localMean(f, sums.temperature, sums);
    const auto cells = static_cast<float>(f.blockCells * f.blockCells);
    f.potential.assign(climate.size(), 0.0f);
    for (std::size_t b = 0; b < climate.size(); ++b) {
        const float land = noise::smoothstep(kLandLow, kLandHigh, sums.land[b] / cells);
        const float inland =
            std::isinf(sea[b]) ? 1.0f
                               : noise::smoothstep(static_cast<float>(params.coastNear),
                                                   static_cast<float>(params.coastFar),
                                                   static_cast<float>(sea[b]));
        f.potential[b] = climate[b] * land * inland;
    }
    f.reach = dilated(f, static_cast<int>(std::ceil(warpReach / f.blockSize)) + 1,
                      [&](std::size_t b) { return f.potential[b] > 0.0f; });
    const double water = std::max(params.corridorClear + params.riverFade,
                                  params.lakeClear + params.lakeFade);
    f.nearWater = dilated(f, static_cast<int>(std::ceil(water / f.blockSize)) + 1,
                          [&](std::size_t b) { return f.water[b] != 0; });
    indexRivers(planet, f);
    return f;
}

float blockAt(const Field& f, const std::vector<float>& values, double x, double z) {
    const double u = (wrapCoordinate(x, f.grid.periodX()) + 0.5 * f.grid.periodX()) / f.blockSize -
                     0.5;
    const double v = (wrapCoordinate(z, f.grid.periodZ()) + 0.5 * f.grid.periodZ()) / f.blockSize -
                     0.5;
    const double fu = std::floor(u);
    const double fv = std::floor(v);
    const int i0 = ((static_cast<int>(fu) % f.bx) + f.bx) % f.bx;
    const int j0 = ((static_cast<int>(fv) % f.bz) + f.bz) % f.bz;
    const int i1 = (i0 + 1) % f.bx;
    const int j1 = (j0 + 1) % f.bz;
    const auto hermite = [](double t) { return t * t * (3.0 - 2.0 * t); };
    const double tx = hermite(u - fu);
    const double tz = hermite(v - fv);
    const auto p = [&](int i, int j) { return static_cast<double>(values[f.block(i, j)]); };
    const double top = p(i0, j0) + (p(i1, j0) - p(i0, j0)) * tx;
    const double bottom = p(i0, j1) + (p(i1, j1) - p(i0, j1)) * tx;
    return static_cast<float>(top + (bottom - top) * tz);
}

float potentialAt(const Field& f, double x, double z) {
    return blockAt(f, f.potential, x, z);
}

bool nearWater(const Field& f, double x, double z) {
    const double u = (wrapCoordinate(x, f.grid.periodX()) + 0.5 * f.grid.periodX()) / f.blockSize;
    const double v = (wrapCoordinate(z, f.grid.periodZ()) + 0.5 * f.grid.periodZ()) / f.blockSize;
    const int bi = std::clamp(static_cast<int>(std::floor(u)), 0, f.bx - 1);
    const int bj = std::clamp(static_cast<int>(std::floor(v)), 0, f.bz - 1);
    return f.nearWater[f.block(bi, bj)] != 0;
}

bool reachAt(const Field& f, double x, double z) {
    const double u = (wrapCoordinate(x, f.grid.periodX()) + 0.5 * f.grid.periodX()) / f.blockSize;
    const double v = (wrapCoordinate(z, f.grid.periodZ()) + 0.5 * f.grid.periodZ()) / f.blockSize;
    const int bi = std::clamp(static_cast<int>(std::floor(u)), 0, f.bx - 1);
    const int bj = std::clamp(static_cast<int>(std::floor(v)), 0, f.bz - 1);
    return f.reach[f.block(bi, bj)] != 0;
}

WaterDistance waterDistance(const Field& f, double x, double z, double limit) {
    if (!nearWater(f, x, z)) {
        return {limit, limit};
    }
    const double cs = f.grid.cellSize;
    const double u = (wrapCoordinate(x, f.grid.periodX()) + 0.5 * f.grid.periodX()) / cs;
    const double v = (wrapCoordinate(z, f.grid.periodZ()) + 0.5 * f.grid.periodZ()) / cs;
    const double fu = std::floor(u);
    const double fv = std::floor(v);
    const auto ci = static_cast<int>(fu);
    const auto cj = static_cast<int>(fv);
    // The point from the center of its cell, meters.
    const double ox = (u - fu - 0.5) * cs;
    const double oz = (v - fv - 0.5) * cs;
    // One more cell: the course of a river reaches the center of the next cell.
    const int r = static_cast<int>(std::ceil(limit / cs)) + 1;
    WaterDistance best{limit, limit};
    for (int dj = -r; dj <= r; ++dj) {
        for (int di = -r; di <= r; ++di) {
            const Field::Water w = f.waterCell(ci + di, cj + dj);
            if (!w.any) {
                continue;
            }
            // The point from the center of the water cell.
            const double px = ox - di * cs;
            const double pz = oz - dj * cs;
            if (w.lake) {
                const double dx = std::max(0.0, std::abs(px) - 0.5 * cs);
                const double dz = std::max(0.0, std::abs(pz) - 0.5 * cs);
                best.lake = std::min(best.lake, dmath::hypot(dx, dz));
                continue;
            }
            best.river = std::min(best.river, segmentDistance(px, pz, w.receiver, cs));
        }
    }
    return best;
}

} // namespace terrain::erg_field

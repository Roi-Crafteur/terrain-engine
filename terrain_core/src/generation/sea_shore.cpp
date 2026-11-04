#include "sea_shore.hpp"

#include "planet/planet_parallel.hpp"
#include "terrain/generation/geography.hpp"

#include <algorithm>
#include <cmath>

namespace terrain::sea_shore {
namespace {

// The distance of every node of the sea to the nearest node of land, in kShoreUnit meters.
std::vector<std::uint16_t> distancesTo(const geography::Grid& g,
                                       const std::vector<std::uint8_t>& land) {
    std::vector<double> seeds(g.nodes(), geography::kNoSource);
    for (std::size_t k = 0; k < g.nodes(); ++k) {
        seeds[k] = land[k] != 0 ? 0.0 : geography::kNoSource;
    }
    const std::vector<double> squared = geography::distanceTransform(g, std::move(seeds));
    std::vector<std::uint16_t> out(g.nodes(), 0);
    const double half = 0.5 * std::min(g.sizeX, g.sizeZ);
    for (std::size_t k = 0; k < g.nodes(); ++k) {
        if (land[k] != 0) {
            continue;
        }
        const double d = squared[k] < geography::kNoSource ? std::sqrt(squared[k]) - half : 1e12;
        out[k] = static_cast<std::uint16_t>(std::clamp(std::round(d / kShoreUnit), 1.0, 65'535.0));
    }
    return out;
}

double bilinear(const Shore& s, const std::vector<std::uint16_t>& field, double x, double z) {
    const geography::Grid& g = s.grid;
    const double u = (wrapCoordinate(x, s.world.x) - g.x(0)) / g.sizeX;
    const double v = (wrapCoordinate(z, s.world.z) - g.z(0)) / g.sizeZ;
    const int i0 = static_cast<int>(std::floor(u));
    const int j0 = static_cast<int>(std::floor(v));
    const double tu = u - i0;
    const double tv = v - j0;
    const auto at = [&](int di, int dj) {
        return static_cast<double>(field[g.index(i0 + di, j0 + dj)]);
    };
    const double top = at(0, 0) + (at(1, 0) - at(0, 0)) * tu;
    const double bottom = at(0, 1) + (at(1, 1) - at(0, 1)) * tu;
    return (top + (bottom - top) * tv) * kShoreUnit;
}

} // namespace

Shore build(const Geography& geo, double bigArea, int threads) {
    Shore s;
    s.world = geo.world();
    s.grid = geography::makeGrid(s.world, kShoreCell);
    const geography::Grid& g = s.grid;
    std::vector<std::uint8_t> land(g.nodes(), 0);
    planet::parallelRows(g.nz, planet::resolveThreads(threads), [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < g.nx; ++i) {
                land[g.index(i, j)] = geo.sampleAt(g.x(i), g.z(j)).coast > 0.0 ? 1 : 0;
            }
        }
    });
    // The large lands: the land masses (8-connected) of at least bigArea.
    int count = 0;
    const std::vector<std::int32_t> label = geography::components(g, land, true, count);
    std::vector<double> area(static_cast<std::size_t>(count), 0.0);
    for (const std::int32_t l : label) {
        if (l >= 0) {
            area[static_cast<std::size_t>(l)] += g.sizeX * g.sizeZ;
        }
    }
    std::vector<std::uint8_t> big(g.nodes(), 0);
    for (std::size_t k = 0; k < g.nodes(); ++k) {
        big[k] = label[k] >= 0 && area[static_cast<std::size_t>(label[k])] >= bigArea ? 1 : 0;
    }
    s.any = distancesTo(g, land);
    s.big = distancesTo(g, big);
    return s;
}

Distances distancesAt(const Shore& s, double x, double z) {
    return {bilinear(s, s.any, x, z), bilinear(s, s.big, x, z)};
}

} // namespace terrain::sea_shore

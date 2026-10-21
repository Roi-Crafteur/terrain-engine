#include "geography_stages.hpp"

#include "planet/planet_parallel.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/noise_octaves.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace terrain::geography {

namespace {

// The kinds of provinces, their shares of the land (far from the belts, and in their forelands),
// the ranges of their uplift and hills (meters, drawn per province) and their massif.
struct Kind {
    double share;
    double forelandShare;
    std::array<float, 2> uplift;
    std::array<float, 2> hills;
    float massif;
};

constexpr std::array<Kind, 7> kKinds = {{
    {0.28, 0.40, {0.0f, 120.0f}, {0.0f, 0.0f}, 0.0f},          // lowland
    {0.10, 0.25, {-250.0f, -80.0f}, {0.0f, 0.0f}, 0.0f},       // sedimentary basin
    {0.12, 0.08, {500.0f, 1000.0f}, {10.0f, 40.0f}, 0.0f},     // high plain
    {0.18, 0.10, {120.0f, 350.0f}, {120.0f, 280.0f}, 0.0f},    // shield with hills
    {0.15, 0.08, {350.0f, 800.0f}, {250.0f, 500.0f}, 0.3f},    // old worn massif
    {0.11, 0.05, {900.0f, 1350.0f}, {40.0f, 120.0f}, 0.0f},    // plateau
    {0.06, 0.04, {800.0f, 1500.0f}, {150.0f, 300.0f}, 0.85f},  // intraplate high relief
}};

// Belts from this strength on make forelands around them.
constexpr float kForelandBelt = 0.3f;

// Within a province, its uplift and hills vary by +-kVariation, by an fbm of this wavelength.
constexpr double kVariationWavelength = 1'500'000.0;
constexpr float kVariation = 0.4f;

// The land undulates by +-kUndulation meters (an fbm of this wavelength), so that its drainage
// converges into rivers instead of running down a tilt in parallel lines.
constexpr double kUndulationWavelength = 400'000.0;
constexpr float kUndulation = 60.0f;

// The shortest offset on a torus between two coordinates of its canonical range.
double shortOffset(double delta, double period) {
    if (delta > 0.5 * period) {
        return delta - period;
    }
    return delta < -0.5 * period ? delta + period : delta;
}

double unit(std::uint64_t seed, std::uint64_t k) {
    return static_cast<double>(noise::deriveSeed(seed, k) >> 11) * 0x1.0p-53;
}

// The kind whose cumulative share reaches u.
std::size_t kindOf(double u, bool foreland) {
    double sum = 0.0;
    for (std::size_t k = 0; k + 1 < kKinds.size(); ++k) {
        sum += foreland ? kKinds[k].forelandShare : kKinds[k].share;
        if (u < sum) {
            return k;
        }
    }
    return kKinds.size() - 1;
}

// One province: its site and what it does to the nodes nearest to it.
struct Province {
    double x;
    double z;
    float uplift;
    float hills;
    float massif;
};

// Sites on a jittered grid of cells about provinceSize wide (a whole number along each side).
struct Sites {
    int nx = 1;
    int nz = 1;
    double sizeX = 1.0;
    double sizeZ = 1.0;
    std::vector<Province> provinces; // row by row

    [[nodiscard]] const Province& at(int i, int j) const {
        return provinces[static_cast<std::size_t>((j % nz + nz) % nz) *
                             static_cast<std::size_t>(nx) +
                         static_cast<std::size_t>((i % nx + nx) % nx)];
    }
};

// Distance from each node to the nearest node of a belt of at least kForelandBelt, meters.
std::vector<double> beltDistance(const Grid& grid, const Tectonics& tectonics) {
    std::vector<double> seed(grid.nodes(), kNoSource);
    for (std::size_t k = 0; k < seed.size(); ++k) {
        seed[k] = tectonics.belt[k] >= kForelandBelt ? 0.0 : kNoSource;
    }
    std::vector<double> distance = distanceTransform(grid, std::move(seed));
    for (double& d : distance) {
        d = d < kNoSource ? std::sqrt(d) : std::numeric_limits<double>::max();
    }
    return distance;
}

Sites placeSites(std::uint64_t seed, const GeographyParams& p, const WorldSize& world,
                 const Grid& grid, const Tectonics& tectonics) {
    const std::vector<double> toBelt = beltDistance(grid, tectonics);
    Sites s;
    s.nx = std::max(1, static_cast<int>(std::lround(world.x / p.provinceSize)));
    s.nz = std::max(1, static_cast<int>(std::lround(world.z / p.provinceSize)));
    s.sizeX = world.x / s.nx;
    s.sizeZ = world.z / s.nz;
    for (int j = 0; j < s.nz; ++j) {
        for (int i = 0; i < s.nx; ++i) {
            const std::uint64_t draws =
                noise::deriveSeed(seed, static_cast<std::uint64_t>(j) * 65536U +
                                            static_cast<std::uint64_t>(i));
            Province v{};
            v.x = -0.5 * world.x + (i + 0.1 + 0.8 * unit(draws, 0)) * s.sizeX;
            v.z = -0.5 * world.z + (j + 0.1 + 0.8 * unit(draws, 1)) * s.sizeZ;
            const int ni = std::min(grid.nx - 1, static_cast<int>((v.x + 0.5 * world.x) /
                                                                   grid.sizeX));
            const int nj = std::min(grid.nz - 1, static_cast<int>((v.z + 0.5 * world.z) /
                                                                   grid.sizeZ));
            const bool foreland = toBelt[grid.index(ni, nj)] < p.forelandDistance;
            const Kind& kind = kKinds[kindOf(unit(draws, 2), foreland)];
            const auto lerp = [](const std::array<float, 2>& range, double t) {
                return static_cast<float>(range[0] + (range[1] - range[0]) * t);
            };
            v.uplift = lerp(kind.uplift, unit(draws, 3));
            v.hills = lerp(kind.hills, unit(draws, 4));
            v.massif = kind.massif;
            s.provinces.push_back(v);
        }
    }
    return s;
}

// The province nearest to the warped point q (canonical), among the 3 x 3 cells around it.
const Province& nearest(const Sites& s, const WorldSize& world, double qx, double qz) {
    const int ci = static_cast<int>(std::floor((qx + 0.5 * world.x) / s.sizeX));
    const int cj = static_cast<int>(std::floor((qz + 0.5 * world.z) / s.sizeZ));
    const Province* best = nullptr;
    double bestDistance = std::numeric_limits<double>::max();
    for (int dj = -1; dj <= 1; ++dj) {
        for (int di = -1; di <= 1; ++di) {
            const Province& v = s.at(ci + di, cj + dj);
            const double dx = shortOffset(v.x - qx, world.x);
            const double dz = shortOffset(v.z - qz, world.z);
            const double d = dx * dx + dz * dz;
            if (d < bestDistance) {
                bestDistance = d;
                best = &v;
            }
        }
    }
    return *best;
}

} // namespace

Provinces buildProvinces(std::uint64_t seed, const GeographyParams& params,
                         const WorldSize& world, const Grid& grid, const Tectonics& tectonics,
                         int threads) {
    const std::uint64_t stream = noise::deriveSeed(seed, kProvinces);
    const Sites sites = placeSites(noise::deriveSeed(stream, 0), params, world, grid, tectonics);
    const auto warp = [&](std::uint64_t s) {
        return noise::FbmOctaves(noise::deriveSeed(stream, s),
                                 {.octaves = 3,
                                  .frequency = 1.0 / (3.0 * params.provinceSize),
                                  .lattices = noise::OctaveLattices::Unrotated},
                                 world);
    };
    const noise::FbmOctaves warpX = warp(1);
    const noise::FbmOctaves warpZ = warp(2);
    const auto slow = [&](std::uint64_t s, double wavelength) {
        return noise::FbmOctaves(noise::deriveSeed(stream, s),
                                 {.octaves = 2,
                                  .frequency = 1.0 / wavelength,
                                  .lattices = noise::OctaveLattices::Unrotated},
                                 world);
    };
    const noise::FbmOctaves variation = slow(3, kVariationWavelength);
    const noise::FbmOctaves undulation = slow(4, kUndulationWavelength);
    Provinces out;
    out.uplift.assign(grid.nodes(), 0.0f);
    out.hills.assign(grid.nodes(), 0.0f);
    out.massif.assign(grid.nodes(), 0.0f);
    planet::parallelRows(grid.nz, threads, [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < grid.nx; ++i) {
                const std::array<float, 2> w = warpX.fbm2DPair(warpZ, grid.x(i), grid.z(j));
                const Province& v = nearest(
                    sites, world,
                    noise::wrapCanonical(grid.x(i) + params.provinceWarp * w[0], world.x),
                    noise::wrapCanonical(grid.z(j) + params.provinceWarp * w[1], world.z));
                const std::size_t k = grid.index(i, j);
                const float vary = 1.0f + kVariation * variation.fbm2D(grid.x(i), grid.z(j));
                out.uplift[k] =
                    v.uplift * vary + kUndulation * undulation.fbm2D(grid.x(i), grid.z(j));
                out.hills[k] = v.hills * vary;
                out.massif[k] = v.massif;
            }
        }
    });
    const int radius = std::max(
        1, static_cast<int>(std::lround(params.provinceBlend / (2.0 * params.cellSize))));
    for (std::vector<float>* field : {&out.uplift, &out.hills, &out.massif}) {
        boxBlur(grid, *field, radius);
    }
    return out;
}

} // namespace terrain::geography

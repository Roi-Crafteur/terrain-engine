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
    {0.27, 0.40, {0.0f, 40.0f}, {0.0f, 0.0f}, 0.0f},           // lowland
    {0.10, 0.25, {-250.0f, -80.0f}, {0.0f, 0.0f}, 0.0f},       // sedimentary basin
    {0.12, 0.08, {500.0f, 1100.0f}, {10.0f, 40.0f}, 0.0f},     // high plain
    {0.17, 0.10, {60.0f, 250.0f}, {100.0f, 250.0f}, 0.0f},     // shield with hills
    {0.15, 0.08, {450.0f, 900.0f}, {250.0f, 500.0f}, 0.3f},    // old worn massif
    {0.10, 0.05, {600.0f, 1000.0f}, {40.0f, 120.0f}, 0.0f},    // plateau
    {0.09, 0.04, {1000.0f, 1800.0f}, {150.0f, 300.0f}, 0.85f}, // intraplate high relief
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

// Whether a belt of at least kForelandBelt lies within distance of node (ni, nj).
bool nearBelt(const Grid& grid, const Tectonics& tectonics, int ni, int nj, double distance) {
    const int ri = static_cast<int>(std::ceil(distance / grid.sizeX));
    const int rj = static_cast<int>(std::ceil(distance / grid.sizeZ));
    for (int dj = -rj; dj <= rj; ++dj) {
        for (int di = -ri; di <= ri; ++di) {
            const double dx = di * grid.sizeX;
            const double dz = dj * grid.sizeZ;
            if (dx * dx + dz * dz < distance * distance &&
                tectonics.belt[grid.index(ni + di, nj + dj)] >= kForelandBelt) {
                return true;
            }
        }
    }
    return false;
}

Sites placeSites(std::uint64_t seed, const GeographyParams& p, const WorldSize& world,
                 const Grid& grid, const Tectonics& tectonics) {
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
            const bool foreland = nearBelt(grid, tectonics, ni, nj, p.forelandDistance);
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

// The noises of the provinces: the warp of their shapes, the variation within each, the
// undulation of the land.
struct Noises {
    noise::FbmOctaves warpX;
    noise::FbmOctaves warpZ;
    noise::FbmOctaves variation;
    noise::FbmOctaves undulation;
};

Noises makeNoises(std::uint64_t stream, const GeographyParams& params, const WorldSize& world) {
    const auto fbm = [&](std::uint64_t s, int octaves, double wavelength) {
        return noise::FbmOctaves(noise::deriveSeed(stream, s),
                                 {.octaves = octaves,
                                  .frequency = 1.0 / wavelength,
                                  .lattices = noise::OctaveLattices::Unrotated},
                                 world);
    };
    return {fbm(1, 3, 3.0 * params.provinceSize), fbm(2, 3, 3.0 * params.provinceSize),
            fbm(3, 2, kVariationWavelength), fbm(4, 2, kUndulationWavelength)};
}

// The uplift, hills and massif of the province nearest to (x, z) after the warp, varied.
std::array<float, 3> provinceFields(const GeographyParams& params, const WorldSize& world,
                                    const Sites& sites, const Noises& n, double x, double z) {
    const std::array<float, 2> w = n.warpX.fbm2DPair(n.warpZ, x, z);
    const Province& v =
        nearest(sites, world, noise::wrapCanonical(x + params.provinceWarp * w[0], world.x),
                noise::wrapCanonical(z + params.provinceWarp * w[1], world.z));
    const float vary = 1.0f + kVariation * n.variation.fbm2D(x, z);
    return {v.uplift * vary + kUndulation * n.undulation.fbm2D(x, z), v.hills * vary, v.massif};
}

// A field of the grid from a field of a coarser grid of the same world, bilinearly.
std::vector<float> upsample(const Grid& from, const Grid& to, const std::vector<float>& field) {
    std::vector<float> out(to.nodes());
    for (int j = 0; j < to.nz; ++j) {
        const double v = (to.z(j) - from.z(0)) / from.sizeZ;
        const int j0 = static_cast<int>(std::floor(v));
        const double tv = v - j0;
        for (int i = 0; i < to.nx; ++i) {
            const double u = (to.x(i) - from.x(0)) / from.sizeX;
            const int i0 = static_cast<int>(std::floor(u));
            const double tu = u - i0;
            const double a =
                field[from.index(i0, j0)] * (1.0 - tu) + field[from.index(i0 + 1, j0)] * tu;
            const double b =
                field[from.index(i0, j0 + 1)] * (1.0 - tu) + field[from.index(i0 + 1, j0 + 1)] * tu;
            out[to.index(i, j)] = static_cast<float>(a * (1.0 - tv) + b * tv);
        }
    }
    return out;
}

} // namespace

Provinces buildProvinces(std::uint64_t seed, const GeographyParams& params,
                         const WorldSize& world, const Grid& grid, const Tectonics& tectonics,
                         int threads) {
    const std::uint64_t stream = noise::deriveSeed(seed, kProvinces);
    const Sites sites = placeSites(noise::deriveSeed(stream, 0), params, world, grid, tectonics);
    const Noises noises = makeNoises(stream, params, world);
    // On a grid half as fine (the fields are blurred over provinceBlend), then bilinearly.
    Grid half;
    half.nx = std::max(1, grid.nx / 2);
    half.nz = std::max(1, grid.nz / 2);
    half.sizeX = world.x / half.nx;
    half.sizeZ = world.z / half.nz;
    std::array<std::vector<float>, 3> coarse;
    for (std::vector<float>& field : coarse) {
        field.assign(half.nodes(), 0.0f);
    }
    planet::parallelRows(half.nz, threads, [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < half.nx; ++i) {
                const std::array<float, 3> f =
                    provinceFields(params, world, sites, noises, half.x(i), half.z(j));
                for (std::size_t n = 0; n < f.size(); ++n) {
                    coarse[n][half.index(i, j)] = f[n];
                }
            }
        }
    });
    Provinces out{upsample(half, grid, coarse[0]), upsample(half, grid, coarse[1]),
                  upsample(half, grid, coarse[2])};
    const int radius = std::max(
        1, static_cast<int>(std::lround(params.provinceBlend / (2.0 * params.cellSize))));
    for (std::vector<float>* field : {&out.uplift, &out.hills, &out.massif}) {
        boxBlur(grid, *field, radius);
    }
    return out;
}

} // namespace terrain::geography

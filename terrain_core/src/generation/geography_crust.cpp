#include "geography_stages.hpp"

#include "planet/planet_parallel.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/noise_octaves.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace terrain::geography {

namespace {

// Candidates are compared on a map this many times coarser along each side.
constexpr int kCoarse = 4;

// A continental core: an ellipse of radius `radius` along its main axis (direction
// (cosAxis, sinAxis)), elongation times longer than wide.
struct Core {
    double x;
    double z;
    double radius;
    double elongation;
    double cosAxis;
    double sinAxis;
};

// The crust of one candidate stream: the cores of its continents, the fbm that roughens them
// and the warp of their coordinates (twins).
struct CrustNoise {
    std::vector<Core> cores;
    noise::FbmOctaves base;
    noise::FbmOctaves warpX;
    noise::FbmOctaves warpZ;
};

// Uniform draws in [0, 1): the k-th draw hashes (seed, k), whatever the platform.
class Draws {
public:
    explicit Draws(std::uint64_t seed) : seed_(seed) {}
    double next() {
        return static_cast<double>(noise::deriveSeed(seed_, counter_++) >> 11) * 0x1.0p-53;
    }

private:
    std::uint64_t seed_;
    std::uint64_t counter_ = 0;
};

double torusOffset(double delta, double period) {
    return delta - period * std::round(delta / period);
}

// torusOffset of two coordinates of the canonical range, without rounding.
double shortOffset(double delta, double period) {
    if (delta > 0.5 * period) {
        return delta - period;
    }
    return delta < -0.5 * period ? delta + period : delta;
}

// Cores spread by best candidate sampling within the middle latitudes, sized so that their
// ellipses hold a little more than the land share, from a few large to several small ones.
std::vector<Core> placeCores(std::uint64_t seed, const GeographyParams& p,
                             const WorldSize& world) {
    Draws draws(seed);
    const int span = std::max(1, p.maxContinents - p.minContinents - 1);
    const int count = std::max(1, p.minContinents + 1 + static_cast<int>(draws.next() * span));
    std::vector<Core> cores;
    double weights = 0.0;
    for (int c = 0; c < count; ++c) {
        Core best{};
        double bestGap = -1.0;
        for (int candidate = 0; candidate < 12 * (c + 1); ++candidate) {
            const double x = (draws.next() - 0.5) * world.x;
            const double z = (draws.next() - 0.5) * world.z * p.coreLatitudes;
            double gap = world.x * world.z;
            for (const Core& o : cores) {
                gap = std::min(gap, dmath::hypot(torusOffset(x - o.x, world.x),
                                                 torusOffset(z - o.z, world.z)));
            }
            if (gap > bestGap) {
                best.x = x;
                best.z = z;
                bestGap = gap;
            }
        }
        const double u = draws.next();
        best.radius = 0.35 + 1.3 * u * std::sqrt(u); // a weight for now
        best.elongation = 1.0 + 1.2 * draws.next();
        const double turns = 0.5 * draws.next();
        best.cosAxis = dmath::cosTurns(turns);
        best.sinAxis = dmath::sinTurns(turns);
        weights += best.radius;
        cores.push_back(best);
    }
    const double land = 1.15 * static_cast<double>(p.landShare) * world.x * world.z;
    for (Core& core : cores) {
        core.radius = std::sqrt(land * core.radius / weights / 3.141592653589793);
    }
    return cores;
}

CrustNoise crustNoise(std::uint64_t seed, const GeographyParams& p, const WorldSize& world,
                      int candidate) {
    const std::uint64_t stream =
        noise::deriveSeed(noise::deriveSeed(seed, kCrust), static_cast<std::uint64_t>(candidate));
    const auto fbm = [&world](std::uint64_t s, int octaves, double wavelength) {
        return noise::FbmOctaves(s,
                                 {.octaves = octaves,
                                  .frequency = 1.0 / wavelength,
                                  .gain = 0.55,
                                  .lattices = noise::OctaveLattices::Unrotated},
                                 world);
    };
    return {placeCores(noise::deriveSeed(stream, 3), p, world),
            fbm(noise::deriveSeed(stream, 0), p.crustOctaves, p.crustWavelength),
            fbm(noise::deriveSeed(stream, 1), 3, p.crustWarpWavelength),
            fbm(noise::deriveSeed(stream, 2), 3, p.crustWarpWavelength)};
}

// The crust value at (x, z): the nearest core in units of its ellipse (1 at its center, 0 on
// its rim) at the warped point, roughened by the fbm, minus the polar bias.
float crustValue(const CrustNoise& n, const GeographyParams& p, const WorldSize& world, double x,
                 double z) {
    const std::array<float, 2> warp = n.warpX.fbm2DPair(n.warpZ, x, z);
    const double wx = noise::wrapCanonical(x + p.crustWarpAmplitude * warp[0], world.x);
    const double wz = noise::wrapCanonical(z + p.crustWarpAmplitude * warp[1], world.z);
    double core = -1.0e9;
    for (const Core& c : n.cores) {
        const double dx = shortOffset(wx - c.x, world.x);
        const double dz = shortOffset(wz - c.z, world.z);
        const double along = dx * c.cosAxis + dz * c.sinAxis;
        const double across = dz * c.cosAxis - dx * c.sinAxis;
        const double d = dmath::hypot(along / c.elongation, across * c.elongation);
        core = std::max(core, 1.0 - d / c.radius);
    }
    const double value = core + p.crustRoughness * n.base.fbm2D(x, z);
    const float latitude = static_cast<float>(std::abs(z) / (0.5 * world.z));
    return static_cast<float>(value) -
           p.polarBias * noise::smoothstep(static_cast<float>(p.polarStart), 1.0f, latitude);
}

std::vector<float> crustValues(const CrustNoise& n, const GeographyParams& p,
                               const WorldSize& world, const Grid& grid, int threads) {
    std::vector<float> values(grid.nodes());
    planet::parallelRows(grid.nz, threads, [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < grid.nx; ++i) {
                values[grid.index(i, j)] = crustValue(n, p, world, grid.x(i), grid.z(j));
            }
        }
    });
    return values;
}

// Land above the quantile that leaves landShare of the nodes above it.
std::vector<std::uint8_t> threshold(const std::vector<float>& values, float landShare,
                                    float& level) {
    level = quantile(values, 1.0 - static_cast<double>(landShare));
    std::vector<std::uint8_t> land(values.size());
    for (std::size_t k = 0; k < values.size(); ++k) {
        land[k] = values[k] > level ? 1 : 0;
    }
    return land;
}

struct Masses {
    int continents = 0;
    double largestShare = 0.0;
};

// 4-connected: land touching by a corner across a strait of one node is two masses, as the
// coasts at the scale of the points draw it.
Masses landMasses(const Grid& grid, const std::vector<std::uint8_t>& land, double continentArea) {
    int count = 0;
    const std::vector<std::int32_t> label = components(grid, land, false, count);
    std::vector<double> area(static_cast<std::size_t>(count), 0.0);
    double total = 0.0;
    for (const std::int32_t l : label) {
        if (l >= 0) {
            area[static_cast<std::size_t>(l)] += grid.sizeX * grid.sizeZ;
            total += grid.sizeX * grid.sizeZ;
        }
    }
    Masses m;
    for (const double a : area) {
        m.continents += a >= continentArea ? 1 : 0;
        m.largestShare = std::max(m.largestShare, total > 0.0 ? a / total : 0.0);
    }
    return m;
}

// How far a candidate is from the wanted continents: 0 when they fit.
double miss(const GeographyParams& p, const Masses& m) {
    const double below = std::max(0, p.minContinents - m.continents);
    const double above = std::max(0, m.continents - p.maxContinents);
    return below + above + 10.0 * std::max(0.0, m.largestShare - p.largestShare);
}

// A candidate: its stream, its crust values on the map and how far they miss.
struct Choice {
    int candidate = -1;
    std::vector<float> values;
    double miss = 0.0;
};

// The land of crust values after the threshold and the filling of small waters.
std::vector<std::uint8_t> landOf(const GeographyParams& p, const Grid& grid,
                                 const std::vector<float>& values, float& level);

// The grid factor times coarser along each side than grid (at least one node).
Grid coarser(const Grid& grid, const WorldSize& world, int factor) {
    Grid g;
    g.nx = std::max(1, grid.nx / factor);
    g.nz = std::max(1, grid.nz / factor);
    g.sizeX = world.x / g.nx;
    g.sizeZ = world.z / g.nz;
    return g;
}

// How far the crust values of a map miss the wanted continents.
double missOn(const GeographyParams& p, const Grid& grid, const std::vector<float>& values) {
    float level = 0.0f;
    return miss(p, landMasses(grid, landOf(p, grid, values, level), p.continentArea));
}

// The first candidate whose map fits, else the one that misses least (the first of equals).
// A candidate is screened on a map kCoarse times coarser first: only those that miss by at most
// one continent there are checked on a map half as fine, at most kChecks of them, and only
// those that fit there on the whole map.
Choice chooseCrust(std::uint64_t seed, const GeographyParams& p, const WorldSize& world,
                   const Grid& grid, int threads) {
    constexpr int kChecks = 8;
    const Grid coarse = coarser(grid, world, kCoarse);
    const Grid half = coarser(grid, world, 2);
    Choice best;
    int checks = 0;
    for (int candidate = 0; candidate < p.crustCandidates && checks < kChecks; ++candidate) {
        const CrustNoise noise = crustNoise(seed, p, world, candidate);
        float level = 0.0f;
        const std::vector<std::uint8_t> screen =
            threshold(crustValues(noise, p, world, coarse, threads), p.landShare, level);
        const bool last = candidate + 1 == p.crustCandidates && best.candidate < 0;
        if (miss(p, landMasses(coarse, screen, p.continentArea)) > 1.0 && !last) {
            continue;
        }
        ++checks;
        Choice choice{candidate, {}, missOn(p, half, crustValues(noise, p, world, half, threads))};
        if (choice.miss == 0.0) {
            choice.values = crustValues(noise, p, world, grid, threads);
            choice.miss = missOn(p, grid, choice.values);
        }
        if (best.candidate < 0 || choice.miss < best.miss) {
            best = std::move(choice);
        }
        if (best.miss == 0.0) {
            break;
        }
    }
    best.candidate = std::max(best.candidate, 0); // no candidate fits even coarsely: the first
    if (best.values.empty()) {
        best.values = crustValues(crustNoise(seed, p, world, best.candidate), p, world, grid,
                                  threads);
    }
    return best;
}

// Waters smaller than inlandSeaArea (4-connected, so that they do not leak between diagonal
// land) become land: lakes are left to the hydrology.
void fillSmallWaters(const GeographyParams& p, const Grid& grid, std::vector<std::uint8_t>& land) {
    std::vector<std::uint8_t> water(land.size());
    for (std::size_t k = 0; k < land.size(); ++k) {
        water[k] = land[k] != 0 ? 0 : 1;
    }
    int count = 0;
    const std::vector<std::int32_t> label = components(grid, water, false, count);
    std::vector<double> area(static_cast<std::size_t>(count), 0.0);
    for (const std::int32_t l : label) {
        if (l >= 0) {
            area[static_cast<std::size_t>(l)] += grid.sizeX * grid.sizeZ;
        }
    }
    // The largest water always stays, so that the world keeps a sea.
    const auto largest = static_cast<std::int32_t>(
        std::max_element(area.begin(), area.end()) - area.begin());
    for (std::size_t k = 0; k < land.size(); ++k) {
        if (label[k] >= 0 && label[k] != largest &&
            area[static_cast<std::size_t>(label[k])] < p.inlandSeaArea) {
            land[k] = 1;
        }
    }
}

bool nextToOther(const Grid& grid, const std::vector<std::uint8_t>& land, int i, int j) {
    const std::uint8_t here = land[grid.index(i, j)];
    return land[grid.index(i + 1, j)] != here || land[grid.index(i - 1, j)] != here ||
           land[grid.index(i, j + 1)] != here || land[grid.index(i, j - 1)] != here;
}

// Distance from the nodes to the nearest node whose land flag equals target, meters (the world
// diagonal when there is none).
std::vector<double> distanceTo(const Grid& grid, const std::vector<std::uint8_t>& land,
                               std::uint8_t target) {
    std::vector<double> seed(grid.nodes(), kNoSource);
    for (std::size_t k = 0; k < land.size(); ++k) {
        seed[k] = land[k] == target ? 0.0 : kNoSource;
    }
    std::vector<double> distance = distanceTransform(grid, std::move(seed));
    const double diagonal = dmath::hypot(grid.periodX(), grid.periodZ());
    for (double& d : distance) {
        d = d < kNoSource ? std::sqrt(d) : diagonal;
    }
    return distance;
}

// Signed distance from each node to the coast, meters: next to the coast (a 4-neighbor of the
// other kind) the crust value over its gradient, within half a node; beyond, the distance to
// the nearest node of the other kind minus half a node, so that the two join.
std::vector<float> coastDistance(const Grid& grid, const std::vector<float>& values, float level,
                                 const std::vector<std::uint8_t>& land) {
    const std::vector<double> toSea = distanceTo(grid, land, 0);
    const std::vector<double> toLand = distanceTo(grid, land, 1);
    const double half = 0.5 * std::min(grid.sizeX, grid.sizeZ);
    std::vector<float> coast(grid.nodes());
    for (int j = 0; j < grid.nz; ++j) {
        for (int i = 0; i < grid.nx; ++i) {
            const std::size_t k = grid.index(i, j);
            const bool isLand = land[k] != 0;
            double d = isLand ? toSea[k] - half : half - toLand[k];
            if (nextToOther(grid, land, i, j)) {
                const double gx = (values[grid.index(i + 1, j)] - values[grid.index(i - 1, j)]) /
                                  (2.0 * grid.sizeX);
                const double gz = (values[grid.index(i, j + 1)] - values[grid.index(i, j - 1)]) /
                                  (2.0 * grid.sizeZ);
                const double gradient = dmath::hypot(gx, gz);
                const double sub = gradient > 0.0 ? (values[k] - level) / gradient : 0.0;
                d = isLand ? std::clamp(sub, 0.02 * half, half) : std::clamp(sub, -half, 0.0);
            }
            coast[k] = static_cast<float>(d);
        }
    }
    return coast;
}

std::vector<std::uint8_t> landOf(const GeographyParams& p, const Grid& grid,
                                 const std::vector<float>& values, float& level) {
    std::vector<std::uint8_t> land = threshold(values, p.landShare, level);
    fillSmallWaters(p, grid, land);
    return land;
}

} // namespace

Crust buildCrust(std::uint64_t seed, const GeographyParams& params, const WorldSize& world,
                 const Grid& grid, int threads) {
    const Choice choice = chooseCrust(seed, params, world, grid, threads);
    Crust crust;
    crust.candidate = choice.candidate;
    float level = 0.0f;
    crust.land = landOf(params, grid, choice.values, level);
    crust.continents = landMasses(grid, crust.land, params.continentArea).continents;
    crust.coast = coastDistance(grid, choice.values, level, crust.land);
    return crust;
}

} // namespace terrain::geography

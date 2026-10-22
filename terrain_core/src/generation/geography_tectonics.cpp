#include "geography_stages.hpp"

#include "planet/planet_parallel.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/noise_octaves.hpp"
#include "terrain/generation/voronoi.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace terrain::geography {

namespace {

// The plates around a node whose boundaries may act on it, nearest first.
constexpr int kNear = 6;

// A boundary converges (diverges) when the convergence of its plates is above kConvergent
// (below its opposite): the more, the stronger its effects, from kWeakest of them on.
constexpr double kConvergent = 0.3;
constexpr double kWeakest = 0.35;

// Along a boundary (fbm of 2 octaves, unrotated): the width and uplift of a belt vary by
// +-kWidthSpread and +-kUpliftSpread, its axis meanders by +-kMeander across the boundary, and
// its strength falls to kGap of it in places, between its ranges.
constexpr double kSpreadWavelength = 800'000.0;
constexpr double kWidthSpread = 0.3;
constexpr double kUpliftSpread = 0.35;
constexpr double kPlateauSpread = 0.1;
constexpr double kMeanderWavelength = 1'200'000.0;
constexpr double kMeander = 120'000.0;
constexpr double kRangesWavelength = 700'000.0;
constexpr float kGap = 0.4f;

// The belt is full within this fraction of its half width from its axis.
constexpr float kBeltCore = 0.3f;

// Continental rifts open along a share of the divergences above kRiftDivergence, their shoulders
// kShoulders rift widths wide.
constexpr double kRiftDivergence = 0.6;
constexpr double kShoulders = 2.5;

// The effects of a boundary taper over kTaper beyond the junctions of its edge, or stop within
// junctionFade where a boundary of the same kind goes on from the junction.
constexpr double kTaper = 700'000.0;

// What the geography draws along a boundary of the plates.
struct Boundary {
    BoundaryKind kind = BoundaryKind::Transform;
    float strength = 0.0f;     // kWeakest to 1, from the convergence
    double halfWidth = 0.0;    // of the belt, before its spread
    int overriding = 0;        // the plate the belt leans toward, the plateau lies on
    double plateauWidth = 0.0; // 0 without plateau
    bool rift = false;         // a divergence that opens a continental rift
    double reach = 0.0;        // no effect farther than this from the bisector
};

// A plate seen from a node: the offset to its nearest image and its squared distance.
struct Site {
    int plate;
    double dx;
    double dz;
    double distance2;
};

struct Context {
    const GeographyParams& params;
    const TectonicPlates& plates;
    std::vector<Boundary> boundaries;
};

// The shortest offset on a torus between two coordinates of its canonical range.
double shortOffset(double delta, double period) {
    if (delta > 0.5 * period) {
        return delta - period;
    }
    return delta < -0.5 * period ? delta + period : delta;
}

// (1 - t^2)^2 inside [-1, 1], 0 outside.
float bump(double t) {
    if (std::abs(t) >= 1.0) {
        return 0.0f;
    }
    const double u = 1.0 - t * t;
    return static_cast<float>(u * u);
}

float step(double edge0, double edge1, double x) {
    return noise::smoothstep(static_cast<float>(edge0), static_cast<float>(edge1),
                             static_cast<float>(x));
}

double unit(std::uint64_t seed, std::uint64_t k) {
    return static_cast<double>(noise::deriveSeed(seed, k) >> 11) * 0x1.0p-53;
}

// The share of land of each plate (the land of the nodes nearest to it after the warp): the plate
// with more land overrides the other in a collision.
std::vector<double> landShares(std::size_t plates, const std::vector<int>& nearest,
                               const std::vector<std::uint8_t>& land) {
    std::vector<double> share(plates, 0.0);
    std::vector<double> all(plates, 0.0);
    for (std::size_t k = 0; k < nearest.size(); ++k) {
        const auto plate = static_cast<std::size_t>(nearest[k]);
        all[plate] += 1.0;
        share[plate] += land[k] != 0 ? 1.0 : 0.0;
    }
    for (std::size_t k = 0; k < plates; ++k) {
        share[k] = all[k] > 0.0 ? share[k] / all[k] : 0.0;
    }
    return share;
}

std::vector<Boundary> drawBoundaries(std::uint64_t seed, const GeographyParams& p,
                                     const TectonicPlates& plates,
                                     const std::vector<double>& landShare) {
    std::vector<Boundary> out;
    for (std::size_t k = 0; k < plates.boundaries().size(); ++k) {
        const PlateBoundary& from = plates.boundaries()[k];
        const std::uint64_t draws = noise::deriveSeed(seed, k);
        const double convergence = static_cast<double>(from.convergence);
        Boundary b;
        b.kind = convergence > kConvergent    ? BoundaryKind::Collision
                 : convergence < -kConvergent ? BoundaryKind::Divergence
                                              : BoundaryKind::Transform;
        b.strength = static_cast<float>(
            kWeakest + (1.0 - kWeakest) * std::clamp((std::abs(convergence) - kConvergent) /
                                                         (1.0 - kConvergent),
                                                     0.0, 1.0));
        b.halfWidth = p.beltWidth * (0.8 + 0.7 * unit(draws, 0));
        const auto a = static_cast<std::size_t>(from.a);
        const auto c = static_cast<std::size_t>(from.b);
        b.overriding = landShare[c] > landShare[a] ? from.b : from.a;
        if (from.length > p.plateauLength && unit(draws, 1) < p.plateauShare) {
            b.plateauWidth = p.plateauWidth * (0.6 + 0.8 * unit(draws, 2));
        }
        b.rift = convergence < -kRiftDivergence && unit(draws, 3) < p.riftShare;
        const double belt = b.halfWidth * (1.0 + kWidthSpread);
        if (b.kind == BoundaryKind::Collision) {
            b.reach = kMeander + std::max(belt * (1.0 + p.beltOffset) +
                                              b.plateauWidth * (1.0 + kWidthSpread),
                                          belt * (1.0 + p.beltOffset) + p.trenchWidth);
        } else if (b.kind == BoundaryKind::Divergence) {
            b.reach = std::max(kShoulders * p.riftWidth, p.ridgeWidth);
        }
        out.push_back(b);
    }
    return out;
}

// Every plate seen from the warped node q, and the kNear nearest of them, nearest first (the
// lowest index first on a tie).
struct Sites {
    std::array<Site, TectonicPlates::kMaxPlates> all;
    int count = 0;
    std::array<Site, kNear> near;
    int nearCount = 0;
};

void findSites(const TectonicPlates& plates, TectonicPlates::Point warped, Sites& sites) {
    const WorldSize& world = plates.world();
    const double qx = noise::wrapCanonical(warped.x, world.x);
    const double qz = noise::wrapCanonical(warped.z, world.z);
    sites.count = static_cast<int>(plates.plates().size());
    sites.nearCount = 0;
    for (int k = 0; k < sites.count; ++k) {
        const Plate& plate = plates.plates()[static_cast<std::size_t>(k)];
        const double dx = shortOffset(plate.x - qx, world.x);
        const double dz = shortOffset(plate.z - qz, world.z);
        const Site s{k, dx, dz, dx * dx + dz * dz};
        sites.all[static_cast<std::size_t>(k)] = s;
        if (sites.nearCount == kNear && !(s.distance2 < sites.near[kNear - 1].distance2)) {
            continue;
        }
        int at = sites.nearCount < kNear ? sites.nearCount++ : kNear - 1;
        while (at > 0 && s.distance2 < sites.near[static_cast<std::size_t>(at - 1)].distance2) {
            sites.near[static_cast<std::size_t>(at)] = sites.near[static_cast<std::size_t>(at - 1)];
            --at;
        }
        sites.near[static_cast<std::size_t>(at)] = s;
    }
}

// The kind of the boundary between two plates (Transform when they are not neighbors).
BoundaryKind kindBetween(const Context& c, int a, int b) {
    const int index = c.plates.boundaryIndex(a, b);
    return index < 0 ? BoundaryKind::Transform
                     : c.boundaries[static_cast<std::size_t>(index)].kind;
}

// How much of the edge between plates a and b faces the node: from the foot of the node on
// their bisector, the gap to the nearest other plate, faded over kTaper (junctionFade where a
// boundary of the same kind goes on from there). 1 along the edge, 0 beyond its junctions.
float edgeWeight(const Context& c, const Sites& sites, const Site& a, const Site& b,
                 double towardB, BoundaryKind kind) {
    const double spacing = dmath::hypot(b.dx - a.dx, b.dz - a.dz);
    // Seen from the foot, q - towardB u (u the unit vector from a to b), every offset grows by
    // towardB u.
    const double ux = towardB * (b.dx - a.dx) / spacing;
    const double uz = towardB * (b.dz - a.dz) / spacing;
    const double onEdge = (a.dx + ux) * (a.dx + ux) + (a.dz + uz) * (a.dz + uz);
    double other = std::numeric_limits<double>::max();
    int third = -1;
    for (int k = 0; k < sites.count; ++k) {
        const Site& s = sites.all[static_cast<std::size_t>(k)];
        const double d2 = (s.dx + ux) * (s.dx + ux) + (s.dz + uz) * (s.dz + uz);
        if (k != a.plate && k != b.plate && d2 < other) {
            other = d2;
            third = k;
        }
    }
    if (third < 0) {
        return 1.0f;
    }
    const bool goesOn =
        kindBetween(c, a.plate, third) == kind || kindBetween(c, b.plate, third) == kind;
    const double gap = (other - onEdge) / (2.0 * c.plates.cellDiameter());
    return step(0.0, goesOn ? c.plates.params().junctionFade : kTaper, gap);
}

// The noises along the boundaries at the node, in [-1, 1].
struct Variation {
    double width = 0.0;
    double uplift = 0.0;
    double meander = 0.0;
    double ranges = 0.0;
};

// The effects of a collision on the node, s meters from the bisector toward the overriding
// plate: the belt, its uplift and the plateau behind it, the trench on the other side.
void collide(const Context& c, const Boundary& b, double s, float weight, const Variation& v,
             Tectonics& t, std::size_t k) {
    const GeographyParams& p = c.params;
    const double w = b.halfWidth * (1.0 + kWidthSpread * v.width);
    const double axis = p.beltOffset * w + kMeander * v.meander;
    const float ranges = kGap + (1.0f - kGap) * step(-0.5, 0.3, v.ranges);
    const float belt =
        b.strength * weight * ranges * (1.0f - step(kBeltCore, 1.0, std::abs(s - axis) / w));
    float plateau = 0.0f;
    if (b.plateauWidth > 0.0) {
        const double width = b.plateauWidth * (1.0 + kWidthSpread * v.width);
        const double start = axis + 0.3 * w;
        plateau = weight * step(axis - 0.3 * w, start, s) *
                  (1.0f - step(start + 0.7 * width, start + width, s));
    }
    // The uplift peaks on the axis, so that the regional elevation slopes down from it (valleys).
    const double beltUplift = p.beltUplift * b.strength * weight * ranges *
                              bump(std::abs(s - axis) / w) * (1.0 + kUpliftSpread * v.uplift);
    const double plateauUplift = p.plateauUplift * plateau * (1.0 + kPlateauSpread * v.uplift);
    const double uplift =
        std::max(beltUplift, plateauUplift) + 0.25 * std::min(beltUplift, plateauUplift);
    t.belt[k] = std::max(t.belt[k], belt);
    t.plateau[k] = std::max(t.plateau[k], plateau);
    t.uplift[k] = std::max(t.uplift[k], static_cast<float>(uplift));
    t.trench[k] =
        std::max(t.trench[k], b.strength * weight * bump((s - axis + 1.35 * w) / p.trenchWidth));
}

void diverge(const Context& c, const Boundary& b, double s, float weight, Tectonics& t,
             std::size_t k) {
    const GeographyParams& p = c.params;
    const float strength = b.strength * weight;
    t.ridge[k] = std::max(t.ridge[k], strength * bump(s / p.ridgeWidth));
    if (b.rift) {
        t.trough[k] = std::max(t.trough[k], strength * bump(s / p.riftWidth));
        t.shoulder[k] = std::max(t.shoulder[k], strength * bump(s / (kShoulders * p.riftWidth)));
    }
}

// Every boundary between two plates near the node acts on it; each field keeps the strongest.
void nodeEffects(const Context& c, const Variation& v, const Sites& sites, Tectonics& t,
                 std::size_t k) {
    for (int i = 0; i < sites.nearCount; ++i) {
        for (int j = i + 1; j < sites.nearCount; ++j) {
            const Site& a = sites.near[static_cast<std::size_t>(i)];
            const Site& b = sites.near[static_cast<std::size_t>(j)];
            const int index = c.plates.boundaryIndex(a.plate, b.plate);
            if (index < 0) {
                continue;
            }
            const Boundary& boundary = c.boundaries[static_cast<std::size_t>(index)];
            const double spacing = dmath::hypot(b.dx - a.dx, b.dz - a.dz);
            const double towardB = (a.distance2 - b.distance2) / (2.0 * spacing);
            if (boundary.kind == BoundaryKind::Transform || std::abs(towardB) >= boundary.reach) {
                continue;
            }
            const float weight = edgeWeight(c, sites, a, b, towardB, boundary.kind);
            if (weight <= 0.0f) {
                continue;
            }
            if (boundary.kind == BoundaryKind::Collision) {
                const double s = boundary.overriding == b.plate ? towardB : -towardB;
                collide(c, boundary, s, weight, v, t, k);
            } else {
                diverge(c, boundary, towardB, weight, t, k);
            }
        }
    }
}

// The slow fields of the plates, computed on a grid kSlowCoarse times coarser and read
// bilinearly at the nodes: the warp of the plates (as an offset) and the noises along the
// boundaries, whose shortest wavelengths are about 250 km. The nearest plate of each coarse
// node gives the share of land of the plates.
constexpr int kSlowCoarse = 2;

enum Slow : std::size_t {
    kOffsetX,
    kOffsetZ,
    kWidthNoise,
    kUpliftNoise,
    kMeanderNoise,
    kRangesNoise,
    kSlowFields
};

struct SlowFields {
    Grid grid;
    std::vector<std::array<double, kSlowFields>> values;
    std::vector<int> nearest;
    std::vector<std::uint8_t> land; // of the node of the map nearest to each coarse node
};

SlowFields slowFields(std::uint64_t stream, const TectonicPlates& plates, const Grid& grid,
                      const Crust& crust, int threads) {
    const WorldSize& world = plates.world();
    const auto along = [&](std::uint64_t s, double wavelength) {
        return noise::FbmOctaves(noise::deriveSeed(stream, s),
                                 {.octaves = 2,
                                  .frequency = 1.0 / wavelength,
                                  .lattices = noise::OctaveLattices::Unrotated},
                                 world);
    };
    const std::array<noise::FbmOctaves, 4> noises = {
        along(1, kSpreadWavelength), along(2, kSpreadWavelength), along(4, kMeanderWavelength),
        along(5, kRangesWavelength)};
    SlowFields slow;
    slow.grid.nx = std::max(1, grid.nx / kSlowCoarse);
    slow.grid.nz = std::max(1, grid.nz / kSlowCoarse);
    slow.grid.sizeX = world.x / slow.grid.nx;
    slow.grid.sizeZ = world.z / slow.grid.nz;
    slow.values.resize(slow.grid.nodes());
    slow.nearest.resize(slow.grid.nodes());
    slow.land.resize(slow.grid.nodes());
    planet::parallelRows(slow.grid.nz, threads, [&](int begin, int end) {
        Sites sites;
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < slow.grid.nx; ++i) {
                const std::size_t k = slow.grid.index(i, j);
                const double x = slow.grid.x(i);
                const double z = slow.grid.z(j);
                const TectonicPlates::Point q = plates.warp(x, z);
                slow.values[k] = {q.x - x, q.z - z};
                for (std::size_t n = 0; n < noises.size(); ++n) {
                    slow.values[k][kWidthNoise + n] = noises[n].fbm2D(x, z);
                }
                findSites(plates, q, sites);
                slow.nearest[k] = sites.near[0].plate;
                slow.land[k] = crust.land[grid.index(
                    static_cast<int>(std::floor(i * slow.grid.sizeX / grid.sizeX + 0.5)),
                    static_cast<int>(std::floor(j * slow.grid.sizeZ / grid.sizeZ + 0.5)))];
            }
        }
    });
    return slow;
}

// The slow fields at node (i, j) of the map, bilinearly.
std::array<double, kSlowFields> slowAt(const SlowFields& slow, const Grid& grid, int i, int j) {
    const double u = (grid.x(i) - slow.grid.x(0)) / slow.grid.sizeX;
    const double v = (grid.z(j) - slow.grid.z(0)) / slow.grid.sizeZ;
    const int i0 = static_cast<int>(std::floor(u));
    const int j0 = static_cast<int>(std::floor(v));
    const double tu = u - i0;
    const double tv = v - j0;
    std::array<double, kSlowFields> out{};
    const std::array<double, 4> weights = {(1.0 - tu) * (1.0 - tv), tu * (1.0 - tv),
                                           (1.0 - tu) * tv, tu * tv};
    for (int c = 0; c < 4; ++c) {
        const auto& values = slow.values[slow.grid.index(i0 + (c & 1), j0 + (c >> 1))];
        for (std::size_t f = 0; f < kSlowFields; ++f) {
            out[f] += weights[static_cast<std::size_t>(c)] * values[f];
        }
    }
    return out;
}

} // namespace

Tectonics buildTectonics(std::uint64_t seed, const GeographyParams& params,
                         const WorldSize& world, const Grid& grid, const Crust& crust,
                         int threads) {
    const std::uint64_t stream = noise::deriveSeed(seed, kPlates);
    const TectonicPlates plates(noise::deriveSeed(stream, 0), params.plates, world);
    const SlowFields slow = slowFields(stream, plates, grid, crust, threads);
    const Context context{params, plates,
                          drawBoundaries(noise::deriveSeed(stream, 3), params, plates,
                                         landShares(plates.plates().size(), slow.nearest,
                                                    slow.land))};
    Tectonics t;
    for (std::vector<float>* field :
         {&t.belt, &t.uplift, &t.trough, &t.shoulder, &t.ridge, &t.trench, &t.plateau}) {
        field->assign(grid.nodes(), 0.0f);
    }
    planet::parallelRows(grid.nz, threads, [&](int begin, int end) {
        Sites sites;
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < grid.nx; ++i) {
                const std::array<double, kSlowFields> f = slowAt(slow, grid, i, j);
                findSites(plates, {grid.x(i) + f[kOffsetX], grid.z(j) + f[kOffsetZ]}, sites);
                const Variation v{f[kWidthNoise], f[kUpliftNoise], f[kMeanderNoise],
                                  f[kRangesNoise]};
                nodeEffects(context, v, sites, t, grid.index(i, j));
            }
        }
    });
    return t;
}

} // namespace terrain::geography

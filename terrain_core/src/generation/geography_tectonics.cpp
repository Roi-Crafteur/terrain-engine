#include "geography_boundaries.hpp"
#include "geography_stages.hpp"

#include "planet/planet_parallel.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
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

// The belt is full within this fraction of its half width from its axis.
constexpr float kBeltCore = 0.3f;

// A collision without a chain keeps kFoothills of its belt and of its uplift: foothills.
constexpr float kFoothills = 0.35f;

// The tails of a belt reach kReachShare of its half width (geography_relief.cpp): narrow chains
// have short tails.
constexpr double kReachShare = 0.5;

// The effects of a boundary taper over kTaper beyond the junctions of its edge, or stop within
// junctionFade where a boundary of the same kind goes on from the junction. Where three
// collisions meet, the weakest stops kDetachGap before the junction, faded in over kTaper beyond
// that: the two others make one chain through the junction, and the weakest a detached range.
constexpr double kTaper = 700'000.0;
constexpr double kDetachGap = 150'000.0;

// The far edge of a plateau is jagged by +-kEdgeJag of its width and fades out over kEdgeFade of
// it.
constexpr double kEdgeJag = 0.15;
constexpr double kEdgeFade = 0.3;

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

// Whether the boundary between plates a and b (index) is the weakest of three collisions meeting
// with plate third (the strength, then the higher index).
bool weakestOfThree(const Context& c, int a, int b, int third, int index) {
    const int ac = c.plates.boundaryIndex(a, third);
    const int bc = c.plates.boundaryIndex(b, third);
    if (ac < 0 || bc < 0) {
        return false;
    }
    const auto weaker = [&c](int x, int y) {
        const float sx = c.boundaries[static_cast<std::size_t>(x)].strength;
        const float sy = c.boundaries[static_cast<std::size_t>(y)].strength;
        return sx < sy || (sx == sy && x > y);
    };
    return c.boundaries[static_cast<std::size_t>(ac)].kind == BoundaryKind::Collision &&
           c.boundaries[static_cast<std::size_t>(bc)].kind == BoundaryKind::Collision &&
           weaker(index, ac) && weaker(index, bc);
}

// How much of the edge between plates a and b (boundary index) faces the node, for its belt and
// for its plateau: from the foot of the node on their bisector, the gap to the nearest other
// plate, faded over kTaper (the belt within junctionFade where a boundary of the same kind goes
// on from there, or detached when it is the weakest of three collisions). 1 along the edge, 0
// beyond its junctions.
struct EdgeWeights {
    float belt = 1.0f;
    float plateau = 1.0f;
};

EdgeWeights edgeWeights(const Context& c, const Sites& sites, const Site& a, const Site& b,
                        double towardB, BoundaryKind kind, int index) {
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
        return {};
    }
    const double gap = (other - onEdge) / (2.0 * c.plates.cellDiameter());
    const float taper = step(0.0, kTaper, gap);
    const bool goesOn =
        kindBetween(c, a.plate, third) == kind || kindBetween(c, b.plate, third) == kind;
    if (!goesOn) {
        return {taper, taper};
    }
    if (kind == BoundaryKind::Collision && weakestOfThree(c, a.plate, b.plate, third, index)) {
        return {step(kDetachGap, kDetachGap + kTaper, gap), taper};
    }
    return {step(0.0, c.plates.params().junctionFade, gap), taper};
}

// The effects of a collision on the node, s meters from the bisector toward the overriding
// plate: the belt (and the reach of its tails), its uplift and the plateau behind it, the trench
// on the other side.
void collide(const Context& c, const Boundary& b, double s, EdgeWeights weights,
             const NodeFrame& f, Tectonics& t, std::size_t k) {
    const GeographyParams& p = c.params;
    const double w = b.halfWidth * (1.0 + kWidthSpread * f.width);
    const double axis = p.beltOffset * w + f.meander;
    const float ranges = rangesFactor(f.ranges);
    const float chain = (b.chainless ? kFoothills : 1.0f) * ranges * weights.belt;
    const float belt = b.strength * chain * (1.0f - step(kBeltCore, 1.0, std::abs(s - axis) / w));
    float plateau = 0.0f;
    if (b.plateauWidth > 0.0) {
        const double width = b.plateauWidth * (1.0 + kWidthSpread * f.width) *
                             (1.0 + kPlateauExtent * f.extent);
        const double start = axis + 0.3 * w;
        const double far = start + width * (1.0 + kEdgeJag * f.edge);
        plateau = weights.plateau * step(axis - 0.3 * w, start, s) *
                  (1.0f - step(far - kEdgeFade * width, far, s));
    }
    // The uplift peaks on the axis, so that the regional elevation slopes down from it (valleys).
    const double beltUplift = p.beltUplift * b.strength * chain * bump(std::abs(s - axis) / w) *
                              (1.0 + kUpliftSpread * f.uplift);
    const double plateauUplift = p.plateauUplift * plateau * (1.0 + kPlateauSpread * f.uplift);
    const double uplift =
        std::max(beltUplift, plateauUplift) + 0.25 * std::min(beltUplift, plateauUplift);
    if (belt > t.belt[k]) {
        t.belt[k] = belt;
        t.reach[k] = static_cast<float>(kReachShare * w);
    }
    t.plateau[k] = std::max(t.plateau[k], plateau);
    t.uplift[k] = std::max(t.uplift[k], static_cast<float>(uplift));
    t.trench[k] = std::max(t.trench[k], b.strength * weights.belt *
                                            bump((s - axis + 1.35 * w) / p.trenchWidth));
}

// The effects of a divergence on the node, s meters from the bisector: the mid-ocean ridge, and
// along a rift its trough (whose floor lies riftDepth below the shoulders whatever the strength)
// and its shoulders.
void diverge(const Context& c, const Boundary& b, double s, float weight, Tectonics& t,
             std::size_t k) {
    const GeographyParams& p = c.params;
    t.ridge[k] = std::max(t.ridge[k], b.strength * weight * bump(s / p.ridgeWidth));
    if (!b.rift) {
        return;
    }
    t.trough[k] = std::max(t.trough[k], weight * bump(s / p.riftWidth));
    const float shoulder = weight * bump(s / (kShoulders * p.riftWidth));
    if (shoulder > t.shoulder[k]) {
        t.shoulder[k] = shoulder;
        t.riftDepth[k] = b.riftDepth;
    }
}

// Every boundary between two plates near the node acts on it; each field keeps the strongest.
void nodeEffects(const Context& c, const NodeFrame& f, const Sites& sites, Tectonics& t,
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
            const EdgeWeights weights =
                edgeWeights(c, sites, a, b, towardB, boundary.kind, index);
            if (weights.belt <= 0.0f && weights.plateau <= 0.0f) {
                continue;
            }
            if (boundary.kind == BoundaryKind::Collision) {
                const double s = boundary.overriding == b.plate ? towardB : -towardB;
                collide(c, boundary, s, weights, f, t, k);
            } else {
                diverge(c, boundary, towardB, weights.belt, t, k);
            }
        }
    }
}

} // namespace

Tectonics buildTectonics(std::uint64_t seed, const GeographyParams& params,
                         const WorldSize& world, const Grid& grid, const Crust& crust,
                         int threads) {
    const std::uint64_t stream = noise::deriveSeed(seed, kPlates);
    const TectonicPlates plates(noise::deriveSeed(stream, 0), params.plates, world);
    const BoundaryFields fields(stream, plates, grid, crust, threads);
    const Context context{params, plates,
                          drawBoundaries(noise::deriveSeed(stream, 3), params, plates,
                                         landShares(plates.plates().size(), fields.nearest(),
                                                    fields.land()))};
    Tectonics t;
    for (std::vector<float>* field : {&t.belt, &t.reach, &t.uplift, &t.trough, &t.shoulder,
                                      &t.riftDepth, &t.ridge, &t.trench, &t.plateau}) {
        field->assign(grid.nodes(), 0.0f);
    }
    planet::parallelRows(grid.nz, threads, [&](int begin, int end) {
        Sites sites;
        for (int j = begin; j < end; ++j) {
            for (int i = 0; i < grid.nx; ++i) {
                const NodeFrame f = fields.at(i, j);
                findSites(plates, f.warped, sites);
                nodeEffects(context, f, sites, t, grid.index(i, j));
            }
        }
    });
    return t;
}

} // namespace terrain::geography

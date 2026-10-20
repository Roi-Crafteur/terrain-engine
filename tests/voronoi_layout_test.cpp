#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/voronoi.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using terrain::BoundaryKind;
using terrain::Plate;
using terrain::PlateBoundary;
using terrain::PlateKind;
using terrain::TectonicPlates;
using terrain::VoronoiParams;

constexpr std::array<std::uint64_t, 16> kSeeds = {
    terrain::kDefaultWorldSeed, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

VoronoiParams withLloyd(int iterations) {
    VoronoiParams params;
    params.lloydIterations = iterations;
    return params;
}

double wrap(double delta, double period) {
    return delta - period * std::round(delta / period);
}

// Owner of each cell of a raster of the torus of the world, raw Voronoi (no warping), row by
// row: kRaster columns and as many rows as fit at about the same pixel size.
constexpr int kRaster = 256;

int rasterRows(const TectonicPlates& plates) {
    return static_cast<int>(std::lround(kRaster * plates.world().z / plates.world().x));
}

std::vector<int> rasterize(const TectonicPlates& plates) {
    const int rows = rasterRows(plates);
    std::vector<int> owner;
    owner.reserve(static_cast<std::size_t>(kRaster * rows));
    const double stepX = plates.world().x / kRaster;
    const double stepZ = plates.world().z / rows;
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < kRaster; ++column) {
            owner.push_back(plates.rawPlateAt((column + 0.5) * stepX - 0.5 * plates.world().x,
                                              (row + 0.5) * stepZ - 0.5 * plates.world().z));
        }
    }
    return owner;
}

// Equivalent diameter 2 * sqrt(area / pi) of every cell, in meters, from the raster.
std::vector<double> cellDiameters(const TectonicPlates& plates) {
    std::vector<int> cells(plates.plates().size(), 0);
    for (const int owner : rasterize(plates)) {
        ++cells[static_cast<std::size_t>(owner)];
    }
    const double pixel = plates.world().x / kRaster * plates.world().z / rasterRows(plates);
    std::vector<double> diameters;
    for (const int count : cells) {
        diameters.push_back(2.0 * std::sqrt(count * pixel / std::numbers::pi));
    }
    return diameters;
}

std::vector<std::vector<int>> neighborLists(const TectonicPlates& plates) {
    std::vector<std::vector<int>> neighbors(plates.plates().size());
    for (const PlateBoundary& b : plates.boundaries()) {
        neighbors[static_cast<std::size_t>(b.a)].push_back(b.b);
        neighbors[static_cast<std::size_t>(b.b)].push_back(b.a);
    }
    return neighbors;
}

// Layouts of every seed, built once: relaxed (the default parameters) and without the Lloyd
// relaxation.
struct Layouts {
    std::vector<TectonicPlates> relaxed;
    std::vector<TectonicPlates> raw;
};

Layouts buildLayouts() {
    Layouts layouts;
    for (const std::uint64_t seed : kSeeds) {
        layouts.relaxed.emplace_back(seed, VoronoiParams{});
        layouts.raw.emplace_back(seed, withLloyd(0));
    }
    return layouts;
}

bool sameLayout(const TectonicPlates& a, const TectonicPlates& b) {
    bool same = a.world() == b.world() && a.cellDiameter() == b.cellDiameter() &&
                a.plates().size() == b.plates().size() &&
                a.boundaries().size() == b.boundaries().size();
    for (std::size_t i = 0; same && i < a.plates().size(); ++i) {
        const Plate& p = a.plates()[i];
        const Plate& q = b.plates()[i];
        same = p.x == q.x && p.z == q.z && p.velocityX == q.velocityX &&
               p.velocityZ == q.velocityZ && p.kind == q.kind;
    }
    for (std::size_t i = 0; same && i < a.boundaries().size(); ++i) {
        const PlateBoundary& p = a.boundaries()[i];
        const PlateBoundary& q = b.boundaries()[i];
        same = p.a == q.a && p.b == q.b && p.kind == q.kind && p.convergence == q.convergence &&
               p.length == q.length;
    }
    return same;
}

// The same seed always gives the same layout; another seed or no relaxation another one.
void testDeterminism(const Layouts& layouts) {
    for (std::size_t i = 0; i < kSeeds.size(); ++i) {
        const TectonicPlates& a = layouts.relaxed[i];
        TEST_CHECK(sameLayout(a, TectonicPlates(kSeeds[i], VoronoiParams{})));
        const TectonicPlates& other = layouts.relaxed[(i + 1) % kSeeds.size()];
        TEST_CHECK(other.plates()[0].x != a.plates()[0].x);
        const TectonicPlates& raw = layouts.raw[i];
        TEST_CHECK(raw.plates().size() == a.plates().size());
        TEST_CHECK(raw.cellDiameter() == a.cellDiameter());
        TEST_CHECK(raw.plates()[0].x != a.plates()[0].x); // the relaxation moved the sites
    }
}

// 20 to 40 plates drawn from the seed, tiling the world: cells of 2 sqrt(area / (pi plates))
// on average, 8,200 to 11,600 km on the default world.
void testPlateCount(const Layouts& layouts) {
    std::set<std::size_t> counts;
    for (const TectonicPlates& plates : layouts.relaxed) {
        const std::size_t n = plates.plates().size();
        counts.insert(n);
        TEST_CHECK(n >= 20 && n <= 40);
        const terrain::WorldSize& world = plates.world();
        TEST_CHECK(world == terrain::WorldSize{});
        const double expected =
            2.0 * std::sqrt(world.x * world.z / (static_cast<double>(n) * std::numbers::pi));
        TEST_CHECK(std::abs(plates.cellDiameter() - expected) < 1.0);
        TEST_CHECK(plates.cellDiameter() > 8'000'000.0 && plates.cellDiameter() < 12'000'000.0);
        bool inside = true;
        for (const Plate& p : plates.plates()) {
            inside = inside && p.x >= -0.5 * world.x && p.x < 0.5 * world.x &&
                     p.z >= -0.5 * world.z && p.z < 0.5 * world.z;
        }
        TEST_CHECK(inside);
    }
    TEST_CHECK(counts.size() >= 6); // the count really depends on the seed

    VoronoiParams fixed;
    fixed.minPlates = 25;
    fixed.maxPlates = 25;
    TEST_CHECK(TectonicPlates(kSeeds[0], fixed).plates().size() == 25);
}

// Every cell is 0.7 to 1.35 times the mean cell diameter across (equivalent diameter), with
// and without the Lloyd relaxation, which makes the cells more even. Measured from 0.769 to
// 1.208 without it and from 0.850 to 1.155 with it over 16 seeds.
void testCellSizes(const Layouts& layouts) {
    double spreadRaw = 0.0;
    double spreadRelaxed = 0.0;
    for (const auto* set : {&layouts.raw, &layouts.relaxed}) {
        for (const TectonicPlates& plates : *set) {
            const std::vector<double> d = cellDiameters(plates);
            const auto [low, high] = std::minmax_element(d.begin(), d.end());
            TEST_CHECK(*low >= 0.7 * plates.cellDiameter());
            TEST_CHECK(*high <= 1.35 * plates.cellDiameter());
            (set == &layouts.raw ? spreadRaw : spreadRelaxed) += *high / *low;
        }
    }
    TEST_CHECK(spreadRelaxed < spreadRaw);
}

// Boundaries are the exact Voronoi neighbors: listed once, symmetric, 3 per plate on the
// torus (a triangulation), and they agree with the neighbors seen on the raster.
void testBoundaryGraph(const TectonicPlates& plates) {
    const auto& boundaries = plates.boundaries();
    const int n = static_cast<int>(plates.plates().size());
    TEST_CHECK(static_cast<int>(boundaries.size()) == 3 * n);
    std::set<std::pair<int, int>> pairs;
    bool valid = true;
    for (std::size_t k = 0; k < boundaries.size(); ++k) {
        const PlateBoundary& b = boundaries[k];
        valid = valid && b.a >= 0 && b.a < b.b && b.b < n && b.length > 0.0 &&
                plates.boundaryIndex(b.a, b.b) == static_cast<int>(k) &&
                plates.boundaryIndex(b.b, b.a) == static_cast<int>(k);
        pairs.insert({b.a, b.b});
    }
    TEST_CHECK(valid);
    TEST_CHECK(pairs.size() == boundaries.size());
    for (int a = 0; a < n; ++a) {
        TEST_CHECK(plates.boundaryIndex(a, a) == -1);
    }

    // Every pair of raster neighbors is a boundary, unless a third plate lies between the two
    // pixels (near a junction the pixels may straddle a narrow cell); every boundary longer
    // than 2 raster cells shows up on the raster.
    const std::vector<int> owner = rasterize(plates);
    std::set<std::pair<int, int>> seen;
    const int rows = rasterRows(plates);
    const auto at = [&](int c, int r) {
        return owner[static_cast<std::size_t>(((r + rows) % rows) * kRaster +
                                              (c + kRaster) % kRaster)];
    };
    const double stepX = plates.world().x / kRaster;
    const double stepZ = plates.world().z / rows;
    bool neighbors = true;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < kRaster; ++c) {
            for (const auto& [dc, dr] : {std::pair{1, 0}, std::pair{0, 1}}) {
                const int self = at(c, r);
                const int other = at(c + dc, r + dr);
                if (other == self) {
                    continue;
                }
                seen.insert({std::min(other, self), std::max(other, self)});
                if (plates.boundaryIndex(self, other) >= 0) {
                    continue;
                }
                bool third = false;
                for (int t = 1; t < 64; ++t) {
                    const double x = (c + 0.5 + dc * t / 64.0) * stepX - 0.5 * plates.world().x;
                    const double z = (r + 0.5 + dr * t / 64.0) * stepZ - 0.5 * plates.world().z;
                    const int plate = plates.rawPlateAt(x, z);
                    third = third || (plate != self && plate != other);
                }
                neighbors = neighbors && third;
            }
        }
    }
    TEST_CHECK(neighbors);
    const double pixel = plates.world().x / kRaster;
    TEST_CHECK(std::all_of(boundaries.begin(), boundaries.end(), [&](const PlateBoundary& b) {
        return b.length < 2.0 * pixel || seen.count({b.a, b.b}) == 1;
    }));
}

// Every plate has at least 3 neighbors and the graph is connected.
void testConnectivity(const TectonicPlates& plates) {
    const auto neighbors = neighborLists(plates);
    TEST_CHECK(std::all_of(neighbors.begin(), neighbors.end(),
                           [](const std::vector<int>& list) { return list.size() >= 3; }));
    std::vector<bool> reached(neighbors.size(), false);
    std::vector<int> stack = {0};
    reached[0] = true;
    while (!stack.empty()) {
        const int p = stack.back();
        stack.pop_back();
        for (const int q : neighbors[static_cast<std::size_t>(p)]) {
            if (!reached[static_cast<std::size_t>(q)]) {
                reached[static_cast<std::size_t>(q)] = true;
                stack.push_back(q);
            }
        }
    }
    TEST_CHECK(std::all_of(reached.begin(), reached.end(), [](bool r) { return r; }));
}

// The kind follows the convergence (collision above cos 60 degrees, divergence below its
// opposite), and the convergence is the relative velocity projected on the direction between
// the sites: the same seen from either plate.
void testBoundaryKinds(const TectonicPlates& plates) {
    std::array<int, 3> kinds{};
    bool consistent = true;
    bool symmetric = true;
    const terrain::WorldSize& world = plates.world();
    for (const PlateBoundary& b : plates.boundaries()) {
        ++kinds[static_cast<std::size_t>(b.kind)];
        const BoundaryKind expected =
            b.convergence >= terrain::kCollisionThreshold    ? BoundaryKind::Collision
            : b.convergence <= -terrain::kCollisionThreshold ? BoundaryKind::Divergence
                                                             : BoundaryKind::Transform;
        consistent = consistent && b.kind == expected && std::abs(b.convergence) <= 1.0f;
        const Plate& pa = plates.plates()[static_cast<std::size_t>(b.a)];
        const Plate& pb = plates.plates()[static_cast<std::size_t>(b.b)];
        const double nx = wrap(pb.x - pa.x, world.x);
        const double nz = wrap(pb.z - pa.z, world.z);
        const double fromA =
            ((pa.velocityX - pb.velocityX) * nx + (pa.velocityZ - pb.velocityZ) * nz) /
            (std::hypot(nx, nz) *
             std::hypot(pa.velocityX - pb.velocityX, pa.velocityZ - pb.velocityZ));
        const double fromB =
            ((pb.velocityX - pa.velocityX) * -nx + (pb.velocityZ - pa.velocityZ) * -nz) /
            (std::hypot(nx, nz) *
             std::hypot(pa.velocityX - pb.velocityX, pa.velocityZ - pb.velocityZ));
        symmetric = symmetric && std::abs(fromA - b.convergence) < 1.0e-5 &&
                    std::abs(fromB - b.convergence) < 1.0e-5;
    }
    TEST_CHECK(consistent);
    TEST_CHECK(symmetric);
    TEST_CHECK(kinds[0] > 0 && kinds[1] > 0 && kinds[2] > 0); // all three kinds
}

void testBoundaries(const Layouts& layouts) {
    for (const TectonicPlates& plates : layouts.relaxed) {
        testBoundaryGraph(plates);
        testConnectivity(plates);
        testBoundaryKinds(plates);
    }
}

// Continents and oceans on every planet, 30% to 50% of land plates; inland seas are enclosed
// by continents, shelves touch one. Inland seas show up on some planets, shelves on most.
void testPlateKinds(const Layouts& layouts) {
    int withInlandSea = 0;
    int withShelf = 0;
    for (const TectonicPlates& plates : layouts.relaxed) {
        const auto neighbors = neighborLists(plates);
        const auto kindOf = [&](int p) {
            return plates.plates()[static_cast<std::size_t>(p)].kind;
        };
        std::array<int, 4> kinds{};
        bool inlandEnclosed = true;
        bool shelfTouches = true;
        for (std::size_t p = 0; p < plates.plates().size(); ++p) {
            const PlateKind kind = plates.plates()[p].kind;
            ++kinds[static_cast<std::size_t>(kind)];
            const auto& around = neighbors[p];
            const auto continents = std::count_if(around.begin(), around.end(), [&](int q) {
                return kindOf(q) == PlateKind::Continent;
            });
            if (kind == PlateKind::InlandSea) {
                inlandEnclosed = inlandEnclosed && continents == static_cast<long>(around.size());
            }
            if (kind == PlateKind::ContinentalShelf) {
                shelfTouches = shelfTouches && continents > 0;
            }
        }
        const double land =
            static_cast<double>(kinds[0]) / static_cast<double>(plates.plates().size());
        TEST_CHECK(kinds[static_cast<std::size_t>(PlateKind::Continent)] > 0);
        TEST_CHECK(kinds[static_cast<std::size_t>(PlateKind::Ocean)] > 0);
        TEST_CHECK(land >= 0.3 && land <= 0.5);
        TEST_CHECK(inlandEnclosed);
        TEST_CHECK(shelfTouches);
        withInlandSea += kinds[static_cast<std::size_t>(PlateKind::InlandSea)] > 0 ? 1 : 0;
        withShelf += kinds[static_cast<std::size_t>(PlateKind::ContinentalShelf)] > 0 ? 1 : 0;
    }
    TEST_CHECK(withInlandSea >= 3);
    TEST_CHECK(withShelf >= 12);
}

bool rejects(const VoronoiParams& params, const terrain::WorldSize& world = {}) {
    try {
        const TectonicPlates plates(1, params, world);
        return false;
    } catch (const std::invalid_argument&) {
        return true;
    }
}

void testInvalidParams() {
    const auto changed = [](auto change) {
        VoronoiParams params;
        change(params);
        return params;
    };
    TEST_CHECK(rejects(VoronoiParams{}, {1000.0, 65536.0})); // not a multiple of 65,536 m
    TEST_CHECK(rejects(changed([](VoronoiParams& p) { p.minPlates = 3; })));
    TEST_CHECK(rejects(changed([](VoronoiParams& p) { p.maxPlates = 19; })));
    TEST_CHECK(
        rejects(changed([](VoronoiParams& p) { p.maxPlates = TectonicPlates::kMaxPlates + 1; })));
    TEST_CHECK(rejects(changed([](VoronoiParams& p) { p.lloydIterations = -1; })));
    TEST_CHECK(rejects(changed([](VoronoiParams& p) { p.warpWavelength = 0.0; })));
    TEST_CHECK(rejects(changed([](VoronoiParams& p) { p.warpAmplitude = -1.0; })));
    TEST_CHECK(rejects(changed([](VoronoiParams& p) { p.coastWidth = 0.0; })));
    TEST_CHECK(rejects(changed(
        [](VoronoiParams& p) { p.collisionWidth = std::numeric_limits<double>::infinity(); })));
    TEST_CHECK(!rejects(changed([](VoronoiParams& p) {
        p.warpAmplitude = 0.0;
        p.detailWarpAmplitude = 0.0;
    })));
}

} // namespace

int main() {
    const Layouts layouts = buildLayouts();
    testDeterminism(layouts);
    testPlateCount(layouts);
    testCellSizes(layouts);
    testBoundaries(layouts);
    testPlateKinds(layouts);
    testInvalidParams();
    return test::result();
}

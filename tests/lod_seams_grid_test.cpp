// Seams between LODs on synthetic grids (lod_seams.cpp). The outline Marching Cubes draws on a
// vertical side of a grid depends only on the 4 corners of each cell side: for every case of a
// cell, a straight chord between the vertices of 2 crossed sides, or a chord around each solid
// corner of an ambiguous side. A grid stitched to a coarser grid (the same density at twice the
// spacing) meets it on their shared side for densities far from linear, with ambiguous sides:
// spheres, tori, waves, blocky boxes, a crater in a slope. Linear densities keep the mesh of
// marchingCubes bit for bit.
#include "terrain/meshing/density_grid.hpp"
#include "terrain/meshing/marching_cubes.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/stream_manager.hpp"

#include "seam_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <set>
#include <tuple>
#include <vector>

namespace {

using seam_test::FacePoint;
using seam_test::Plane;
using seam_test::Segment;
using terrain::DensityGrid;
using terrain::kGridSamples;
using terrain::Mesh;
using terrain::TileData;

constexpr int kLast = kGridSamples - 1;

// Vertices of a coarse side on the fine outline: float rounding only.
constexpr double kCoarseTolerance = 1e-5;

// A vertical side of the grid with its seam bit: the plane x (alongX) or z = at, in grid units.
struct Side {
    unsigned seam = 0;
    Plane plane;
};

constexpr std::array<Side, 4> kSides{{
    {terrain::kSeamNegX, {true, 0.0}},
    {terrain::kSeamPosX, {true, kLast}},
    {terrain::kSeamNegZ, {false, 0.0}},
    {terrain::kSeamPosZ, {false, kLast}},
}};

// A mesh in grid units as a tile at the origin, its vertices scaled to world units.
TileData asTile(const Mesh& mesh, double scale, double x0, double y0, double z0) {
    TileData data;
    data.originX = x0;
    data.originY = y0;
    data.originZ = z0;
    data.mesh = mesh;
    for (terrain::Vec3f& v : data.mesh.vertices) {
        v = {static_cast<float>(v.x * scale), static_cast<float>(v.y * scale),
             static_cast<float>(v.z * scale)};
    }
    return data;
}

using Key = std::tuple<double, double, double, double>;

Key keyOf(FacePoint p, FacePoint q) {
    if (std::tie(q.u, q.y) < std::tie(p.u, p.y)) {
        std::swap(p, q);
    }
    return {p.u, p.y, q.u, q.y};
}

// The outline expected on the square [u0, u0 + 1] x [y0, y0 + 1] of a side whose corners hold
// -1 (solid) or +1 (air): vertices in the middle of the crossed sides.
std::set<Key> expectedOutline(const std::array<bool, 4>& solid, double u0, double y0) {
    const std::array<FacePoint, 4> corner = {
        {{u0, y0}, {u0 + 1.0, y0}, {u0 + 1.0, y0 + 1.0}, {u0, y0 + 1.0}}};
    std::array<FacePoint, 4> middle{};
    std::vector<std::size_t> crossed;
    for (std::size_t k = 0; k < 4; ++k) {
        const std::size_t next = (k + 1) % 4;
        middle[k] = {(corner[k].u + corner[next].u) / 2.0, (corner[k].y + corner[next].y) / 2.0};
        if (solid[k] != solid[next]) {
            crossed.push_back(k);
        }
    }
    std::set<Key> outline;
    if (crossed.size() == 2) {
        outline.insert(keyOf(middle[crossed[0]], middle[crossed[1]]));
    } else if (crossed.size() == 4) {
        for (std::size_t k = 0; k < 4; ++k) {
            if (solid[k]) {
                outline.insert(keyOf(middle[(k + 3) % 4], middle[k]));
            }
        }
    }
    return outline;
}

// Every case of the cell touching each vertical side of the grid in its middle: the outline
// on that side is the expected one, ambiguous sides included.
void testEveryCase() {
    int ambiguous = 0;
    int wrong = 0;
    for (const Side& side : kSides) {
        const int fixed = static_cast<int>(side.plane.at) == 0 ? 0 : kLast - 1;
        for (unsigned cube = 0; cube < 256; ++cube) {
            DensityGrid grid(1.0f);
            std::array<bool, 4> solid{};
            for (unsigned corner = 0; corner < 8; ++corner) {
                const int d = static_cast<int>(corner & 1U);
                const int dy = static_cast<int>((corner >> 1) & 1U);
                const int dz = static_cast<int>((corner >> 2) & 1U);
                const bool isSolid = ((cube >> corner) & 1U) != 0;
                const int x = side.plane.alongX ? fixed + d : 8 + dz;
                const int z = side.plane.alongX ? 8 + dz : fixed + d;
                grid.set(x, 8 + dy, z, isSolid ? -1.0f : 1.0f);
                // The corners on the side, in turn: (u0, y0), (u0 + 1, y0), (u0 + 1, y0 + 1)...
                const auto along = static_cast<std::size_t>(dz);
                if (fixed + d == static_cast<int>(side.plane.at)) {
                    solid[dy == 0 ? along : 3U - along] = isSolid;
                }
            }
            std::set<Key> actual;
            for (const Segment& s :
                 seam_test::faceSegments(asTile(terrain::marchingCubes(grid), 1.0, 0, 0, 0),
                                         side.plane)) {
                const auto inside = [](FacePoint p) {
                    return p.u >= 8.0 && p.u <= 9.0 && p.y >= 8.0 && p.y <= 9.0;
                };
                if (inside(s.first) && inside(s.second)) {
                    actual.insert(keyOf(s.first, s.second));
                }
            }
            const std::set<Key> expected = expectedOutline(solid, 8.0, 8.0);
            ambiguous += expected.size() == 2 ? 1 : 0;
            wrong += actual == expected ? 0 : 1;
        }
    }
    std::printf("every case of a cell on the 4 vertical sides: %d ambiguous sides, %d outlines "
                "differ from the chords\n",
                ambiguous, wrong);
    TEST_CHECK(ambiguous > 0);
    TEST_CHECK(wrong == 0);
}

// Pseudo-random draws (SplitMix64), the same on every platform.
class Draws {
public:
    explicit Draws(std::uint64_t seed) : state_(seed) {}
    // In [low, high).
    double uniform(double low, double high) {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        z ^= z >> 31;
        return low + (high - low) * static_cast<double>(z >> 11) * 0x1.0p-53;
    }

private:
    std::uint64_t state_;
};

using Density = std::function<double(double, double, double)>;

// The grid of the density at (x0, y0, z0) + (i, j, k) * spacing.
DensityGrid sampled(const Density& density, double spacing, double x0, double y0, double z0) {
    DensityGrid grid(0.0f);
    for (int k = 0; k < kGridSamples; ++k) {
        for (int j = 0; j < kGridSamples; ++j) {
            for (int i = 0; i < kGridSamples; ++i) {
                grid.set(i, j, k,
                         static_cast<float>(density(x0 + i * spacing, y0 + j * spacing,
                                                    z0 + k * spacing)));
            }
        }
    }
    return grid;
}

// The sample (a along y, b along the side) of a side of the grid.
float sideAt(const DensityGrid& grid, const Side& side, int a, int b) {
    const auto fixed = static_cast<int>(side.plane.at);
    return side.plane.alongX ? grid.at(fixed, a, b) : grid.at(b, a, fixed);
}

// Coarse cells of the side (2 x 2 cells) whose corners alternate solid and air.
int ambiguousCells(const DensityGrid& grid, const Side& side) {
    int count = 0;
    for (int b = 0; b < kLast; b += 2) {
        for (int a = 0; a < kLast; a += 2) {
            const bool s00 = sideAt(grid, side, a, b) < terrain::kIsoLevel;
            const bool s01 = sideAt(grid, side, a, b + 2) < terrain::kIsoLevel;
            const bool s11 = sideAt(grid, side, a + 2, b + 2) < terrain::kIsoLevel;
            const bool s10 = sideAt(grid, side, a + 2, b) < terrain::kIsoLevel;
            count += s00 == s11 && s01 == s10 && s00 != s01 ? 1 : 0;
        }
    }
    return count;
}

// Centers of the coarse cells of the side whose sign is no longer that of their bilinear value.
int settledCenters(const DensityGrid& stitched, const Side& side) {
    int count = 0;
    for (int b = 1; b < kLast; b += 2) {
        for (int a = 1; a < kLast; a += 2) {
            const float mean = (sideAt(stitched, side, a - 1, b - 1) +
                                sideAt(stitched, side, a + 1, b - 1) +
                                sideAt(stitched, side, a - 1, b + 1) +
                                sideAt(stitched, side, a + 1, b + 1)) /
                               4.0f;
            const bool solid = sideAt(stitched, side, a, b) < terrain::kIsoLevel;
            count += solid != (mean < terrain::kIsoLevel) ? 1 : 0;
        }
    }
    return count;
}

// Largest distance from a vertex of `from` on the side of the fine grid to the outline of `to`.
double largestGap(const TileData& from, const TileData& to, Plane plane) {
    const std::vector<Segment> outline = seam_test::faceSegments(to, plane);
    double largest = 0.0;
    for (const FacePoint p : seam_test::facePoints(from, plane)) {
        if (p.u >= 0.0 && p.u <= kLast && p.y >= 0.0 && p.y <= kLast) {
            largest = std::max(largest, seam_test::distanceToOutline(p, outline));
        }
    }
    return largest;
}

struct Totals {
    double fineGap = 0.0;      // stitched and snapped, fine to coarse
    double coarseGap = 0.0;    // coarse to fine
    double unsnappedGap = 0.0; // stitched, meshed by marchingCubes alone
    int ambiguous = 0;
    int settled = 0;
};

// The fine grid at the origin (spacing 1) and the coarse grid (spacing 2) beyond the side, the
// side of the fine grid in the slot (u, y) of the side of the coarse grid.
void checkSide(const Density& density, const Side& side, int slotU, int slotY, Totals& totals) {
    const double normal = side.plane.at == 0.0 ? -2.0 * kLast : kLast;
    const double u = -kLast * slotU;
    const double y = -kLast * slotY;
    const DensityGrid coarse = side.plane.alongX ? sampled(density, 2.0, normal, y, u)
                                                 : sampled(density, 2.0, u, y, normal);
    const TileData coarseTile =
        side.plane.alongX ? asTile(terrain::marchingCubes(coarse), 2.0, normal, y, u)
                          : asTile(terrain::marchingCubes(coarse), 2.0, u, y, normal);
    DensityGrid stitched = sampled(density, 1.0, 0.0, 0.0, 0.0);
    terrain::stitchSeams(stitched, side.seam);
    const TileData fine = asTile(terrain::stitchedMesh(stitched, side.seam), 1.0, 0, 0, 0);
    const TileData unsnapped = asTile(terrain::marchingCubes(stitched), 1.0, 0, 0, 0);
    totals.fineGap = std::max(totals.fineGap, largestGap(fine, coarseTile, side.plane));
    totals.coarseGap = std::max(totals.coarseGap, largestGap(coarseTile, fine, side.plane));
    totals.unsnappedGap =
        std::max(totals.unsnappedGap, largestGap(unsnapped, coarseTile, side.plane));
    totals.ambiguous += ambiguousCells(stitched, side);
    totals.settled += settledCenters(stitched, side);
}

void checkDensity(const char* name, const Density& density, Totals& all) {
    Totals totals;
    for (const Side& side : kSides) {
        for (int slot = 0; slot < 4; ++slot) {
            checkSide(density, side, slot % 2, slot / 2, totals);
        }
    }
    std::printf("  %-8s gaps %.2g fine to coarse, %.2g coarse to fine (%.2g without snapping); "
                "%d ambiguous coarse cells, %d centers settled\n",
                name, totals.fineGap, totals.coarseGap, totals.unsnappedGap, totals.ambiguous,
                totals.settled);
    all.fineGap = std::max(all.fineGap, totals.fineGap);
    all.coarseGap = std::max(all.coarseGap, totals.coarseGap);
    all.unsnappedGap = std::max(all.unsnappedGap, totals.unsnappedGap);
    all.ambiguous += totals.ambiguous;
    all.settled += totals.settled;
}

double length(double x, double y, double z) {
    return std::sqrt(x * x + y * y + z * z);
}

// Densities far from linear, drawn at random around the fine grid: their sides cross the
// sides of both grids in every way.
void testStitchedSides() {
    Draws draws(0x5EA35);
    Totals all;
    for (int n = 0; n < 6; ++n) {
        const double cx = draws.uniform(-6.0, 22.0);
        const double cy = draws.uniform(2.0, 14.0);
        const double cz = draws.uniform(-6.0, 22.0);
        const double r = draws.uniform(3.0, 12.0);
        const double p = draws.uniform(0.0, 6.28);
        std::printf("draw %d:\n", n);
        checkDensity("sphere", [=](double x, double y, double z) {
            return length(x - cx, y - cy, z - cz) - r;
        }, all);
        checkDensity("torus", [=](double x, double y, double z) {
            return length(length(x - cx, 0.0, z - cz) - r, y - cy, 0.0) - 0.4 * r;
        }, all);
        checkDensity("waves", [=](double x, double y, double z) {
            return std::sin(0.9 * x + 0.4 * y + p) * std::cos(0.7 * z - 0.3 * y + 2.0 * p) +
                   0.3 * std::sin(0.5 * y + 3.0 * p);
        }, all);
        checkDensity("box", [=](double x, double y, double z) {
            const double box = std::max({std::abs(x - cx) - 0.5 * r, std::abs(y - cy) - 0.3 * r,
                                         std::abs(z - cz) - 0.4 * r});
            return std::max(y - 8.0 - 0.2 * x, -box);
        }, all);
        checkDensity("crater", [=](double x, double y, double z) {
            return std::max(y - (0.35 * x - 0.2 * z + 7.0), r - length(x - cx, y - cy, z - cz));
        }, all);
    }
    std::printf("synthetic seams: gaps %.2g fine to coarse, %.2g coarse to fine (%.2g without "
                "snapping); %d ambiguous coarse cells, %d centers settled\n",
                all.fineGap, all.coarseGap, all.unsnappedGap, all.ambiguous, all.settled);
    TEST_CHECK(all.fineGap <= seam_test::outlineTolerance(0));
    TEST_CHECK(all.coarseGap <= kCoarseTolerance);
    TEST_CHECK(all.unsnappedGap > 10.0 * seam_test::outlineTolerance(0));
    TEST_CHECK(all.ambiguous > 0 && all.settled > 0);
}

// Densities linear along y, as the natural ground y - height(x, z), even far from the origin,
// and linear in every direction: the stitched sides already meet the coarse outline, nothing
// moves, the mesh is that of marchingCubes bit for bit.
void testLinearAlongY() {
    Draws draws(0x11AE);
    int meshed = 0;
    bool same = true;
    for (int n = 0; n < 40; ++n) {
        const double base = draws.uniform(-5000.0, 9000.0);
        const double a = draws.uniform(-3.0, 3.0);
        const double b = draws.uniform(-3.0, 3.0);
        const double f = draws.uniform(0.05, 0.9);
        const Density ground = [=](double x, double y, double z) {
            return (y + base) - (base + 8.0 + a * std::sin(f * x) + b * std::cos(f * z + a));
        };
        const Density plane = [=](double x, double y, double z) {
            return a * x + (y + base) * (1.0 + 0.1 * b) - b * z - base - 8.0;
        };
        for (const Density& density : {ground, plane}) {
            DensityGrid grid = sampled(density, 1.0, 0.0, 0.0, 0.0);
            terrain::stitchSeams(grid, 0xFFU);
            const Mesh mesh = terrain::marchingCubes(grid);
            meshed += mesh.indices.empty() ? 0 : 1;
            same = same && terrain::stitchedMesh(grid, 0xFFU) == mesh;
        }
    }
    std::printf("linear along y: %d meshed grids, the same mesh as marchingCubes: %s\n", meshed,
                same ? "yes" : "no");
    TEST_CHECK(meshed > 30);
    TEST_CHECK(same);
}

// Grids all solid, all air, or with samples on the iso level on the sides.
void testEdgeCases() {
    for (const float fill : {-1.0f, 1.0f, 0.0f, -0.0f}) {
        DensityGrid grid(fill);
        terrain::stitchSeams(grid, 0xFFU);
        TEST_CHECK(terrain::stitchedMesh(grid, 0xFFU) == terrain::marchingCubes(grid));
    }
    // A sphere through samples of the sides: its density is 0 at some of them.
    DensityGrid grid = sampled([](double x, double y, double z) {
        return length(x - 8.0, y - 8.0, z) - 5.0;
    }, 1.0, 0.0, 0.0, 0.0);
    terrain::stitchSeams(grid, 0xFFU);
    const Mesh mesh = terrain::stitchedMesh(grid, 0xFFU);
    TEST_CHECK(!mesh.indices.empty() && mesh.vertices.size() == mesh.normals.size());
    TEST_CHECK(terrain::stitchedMesh(grid, 0U) == terrain::marchingCubes(grid));
}

} // namespace

int main() {
    testEveryCase();
    testStitchedSides();
    testLinearAlongY();
    testEdgeCases();
    return test::result();
}

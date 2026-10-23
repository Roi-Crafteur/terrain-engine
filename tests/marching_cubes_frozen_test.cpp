// The live marchingCubes against its frozen reference of commit 7cf4877 (tests/frozen), bit for
// bit (vertices, normals, indices): on the density grids of the relief (mountain and plain
// chunks of the default world and of the planet, at the spacings of LOD 0 to 3) and of the
// caves around cave entrances (the details of LOD 0 and 1), each also with seams stitched; and
// on synthetic grids (spheres, planes, pseudo-random densities) and edge cases (all solid, all
// air, densities equal to the iso level, +-0, subnormals, infinities, NaN). The golden checksums
// of the tiles cover the meshing of a few tiles: this test covers every case of a cell.
#include "frozen/frozen_marching_cubes.hpp"

#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/heightfield_density.hpp"
#include "terrain/meshing/density_grid.hpp"
#include "terrain/meshing/marching_cubes.hpp"
#include "terrain/streaming/cave_tiles.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "bit_exact_support.hpp"
#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

using bit_exact::same;
using terrain::DensityGrid;
using terrain::kGridSamples;
using terrain::Mesh;

// Seam masks stitched on the grids of the world: none, each side and corner, and all of them.
constexpr std::array<unsigned, 6> kSeams = {
    0U,
    terrain::kSeamNegX | terrain::kSeamPosZ,
    terrain::kSeamPosX | terrain::kSeamNegZ | terrain::kSeamPosXNegZ,
    terrain::kSeamNegXNegZ | terrain::kSeamNegXPosZ,
    terrain::kSeamPosXPosZ,
    0xFFU,
};

bool same(const terrain::Vec3f& a, const terrain::Vec3f& b) {
    return same(a.x, b.x) && same(a.y, b.y) && same(a.z, b.z);
}

bool sameMesh(const Mesh& a, const Mesh& b) {
    if (a.vertices.size() != b.vertices.size() || a.normals.size() != b.normals.size() ||
        a.indices != b.indices) {
        return false;
    }
    for (std::size_t i = 0; i < a.vertices.size(); ++i) {
        if (!same(a.vertices[i], b.vertices[i]) || !same(a.normals[i], b.normals[i])) {
            return false;
        }
    }
    return true;
}

// Meshes a grid both ways; counts the grids whose mesh holds triangles.
class Comparison {
public:
    explicit Comparison(const char* name) : tally_(name), name_(name) {}
    ~Comparison() { std::printf("%-34s %9zu grids with triangles\n", name_, meshed_); }
    Comparison(const Comparison&) = delete;
    Comparison& operator=(const Comparison&) = delete;

    void check(const DensityGrid& grid, double where) {
        const Mesh live = terrain::marchingCubes(grid);
        tally_.check(sameMesh(live, frozen::marchingCubes(grid)), {where, 0.0}, "marchingCubes");
        if (!live.indices.empty()) {
            ++meshed_;
        }
    }
    // The grid, then the grid with each mask of kSeams stitched.
    void checkWithSeams(const DensityGrid& grid, double where) {
        for (const unsigned seams : kSeams) {
            DensityGrid stitched = grid;
            terrain::stitchSeams(stitched, seams);
            check(stitched, where);
        }
    }
    [[nodiscard]] std::size_t meshed() const { return meshed_; }

private:
    bit_exact::Tally tally_;
    const char* name_;
    std::size_t meshed_ = 0;
};

// The grid of density(x, y, z) at (x0, y0, z0) + (i, j, k) * spacing.
template <class F>
DensityGrid gridOf(const F& density, double x0, double y0, double z0, double spacing) {
    return terrain::sampleDensity(
        [&](double i, double j, double k) {
            return density(x0 + i * spacing, y0 + j * spacing, z0 + k * spacing);
        },
        0.0, 0.0, 0.0);
}

// The levels of a chunk at the spacing of each LOD around the ground at its corner: the one
// holding the ground, the one below and the one above.
void checkRelief(Comparison& comparison, const terrain::Heightfield& field,
                 terrain::ChunkCoord chunk) {
    const terrain::HeightfieldDensity density(field);
    const double x0 = chunk.x * terrain::kChunkSizeMeters;
    const double z0 = chunk.z * terrain::kChunkSizeMeters;
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        const double spacing = static_cast<double>(1 << lod);
        const double height = terrain::kChunkCells * spacing;
        const double ground = std::floor(field.heightAt(x0, z0) / height) * height;
        for (const double y0 : {ground - height, ground, ground + height}) {
            comparison.checkWithSeams(gridOf(density, x0, y0, z0, spacing), x0);
        }
    }
    // The grid of the columns of the chunk, as the tiles of LOD 0 build it.
    const terrain::ChunkColumns columns = terrain::sampleChunkColumns(field, x0, z0);
    const double low = std::floor(columns.minHeight / terrain::kChunkSizeMeters);
    comparison.check(terrain::densityFromColumns(columns, low * terrain::kChunkSizeMeters), z0);
}

void checkWorlds() {
    Comparison comparison("relief grids");
    terrain::HeightfieldParams planet;
    planet.macroContinent = 0.25f;
    planet.plates.amplitude = 0.5f;
    for (const terrain::HeightfieldParams& params : {terrain::HeightfieldParams{}, planet}) {
        const terrain::Heightfield field(streaming_test::kSeed, params);
        checkRelief(comparison, field, streaming_test::mountainChunk(field));
        checkRelief(comparison, field,
                    {streaming_test::kCenterChunkX, streaming_test::kCenterChunkZ});
    }
    // Where the ground is flat (the floors of the valleys in U) the grids above and below it
    // miss it.
    TEST_CHECK(comparison.meshed() > 90);
}

// The carved density of the grid at (x0, y0, z0) + (i, j, k) * spacing with the detail of a
// LOD: the values of caveDensityAt, from one sampler of the box of the grid.
DensityGrid caveGrid(const terrain::TerrainTiles& tiles, int lod, double x0, double y0, double z0) {
    const double spacing = static_cast<double>(1 << lod);
    const double size = terrain::kChunkCells * spacing;
    const terrain::CaveGround& ground = tiles.caveGround();
    terrain::CaveSampler sampler(*tiles.caves(), ground, terrain::caveDetailForLod(lod),
                                 {x0, y0, z0, x0 + size, y0 + size, z0 + size});
    DensityGrid grid(0.0f);
    for (int k = 0; k < kGridSamples; ++k) {
        for (int i = 0; i < kGridSamples; ++i) {
            const double x = x0 + i * spacing;
            const double z = z0 + k * spacing;
            const float height = ground.height(x, z);
            const terrain::CaveColumn column = sampler.column(x, z, height);
            for (int j = 0; j < kGridSamples; ++j) {
                const double y = y0 + j * spacing;
                const auto relief = static_cast<float>(y - static_cast<double>(height));
                grid.set(i, j, k, sampler.density(column, y, relief));
            }
        }
    }
    return grid;
}

// Grids around the bottom of the first cave entrances near the center, where the entrance
// opens into its chamber, with the detail of LOD 0 and 1.
void checkCaves() {
    Comparison comparison("cave grids");
    const terrain::TerrainTiles tiles(streaming_test::kSeed, {}, {}, terrain::CaveParams{});
    const std::vector<terrain::CaveEntrance> entrances = streaming_test::entrancesNearOrigin(tiles);
    TEST_CHECK(entrances.size() >= 6);
    for (std::size_t n = 0; n < entrances.size() && n < 6; ++n) {
        const terrain::CaveEntrance& entrance = entrances[n];
        for (int lod = 0; lod < 2; ++lod) {
            const double half = 0.5 * terrain::kChunkCells * static_cast<double>(1 << lod);
            comparison.checkWithSeams(caveGrid(tiles, lod, std::floor(entrance.bottomX - half),
                                               std::floor(entrance.bottomY - half),
                                               std::floor(entrance.bottomZ - half)),
                                      entrance.bottomX);
        }
    }
    TEST_CHECK(comparison.meshed() > 30);
}

// Pseudo-random state, one 64-bit word per draw.
class Draws {
public:
    explicit Draws(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    // In [-1, 1).
    float unit() { return static_cast<float>(static_cast<double>(next() >> 11) * 0x1p-52 - 1.0); }

private:
    std::uint64_t state_;
};

template <class F>
DensityGrid gridFrom(F&& value) {
    DensityGrid grid(0.0f);
    for (int z = 0; z < kGridSamples; ++z) {
        for (int y = 0; y < kGridSamples; ++y) {
            for (int x = 0; x < kGridSamples; ++x) {
                grid.set(x, y, z, value(x, y, z));
            }
        }
    }
    return grid;
}

// Spheres, planes and smooth fields, whose surfaces cross the cells in every direction.
void checkShapes(Comparison& comparison) {
    for (int n = 0; n < 12; ++n) {
        const double cx = 8.25 + 0.37 * n;
        const double radius = 2.0 + 0.9 * n;
        const auto sphere = [cx, radius](double x, double y, double z) {
            const double dx = x - cx;
            const double dy = y - 7.75;
            const double dz = z - 8.5;
            return std::sqrt(dx * dx + dy * dy + dz * dz) - radius;
        };
        comparison.check(terrain::sampleDensity(sphere, 0.0, 0.0, 0.0), n);
        const double slope = -2.0 + 0.4 * n;
        const auto plane = [slope](double x, double y, double z) {
            return y - slope * x - 0.3 * z - 6.0;
        };
        comparison.check(terrain::sampleDensity(plane, 0.0, 0.0, 0.0), n);
        const auto waves = [n](double x, double y, double z) {
            return y - 8.0 - 3.0 * std::sin(0.4 * x + n) * std::cos(0.3 * z);
        };
        comparison.check(terrain::sampleDensity(waves, 0.0, 0.0, 0.0), n);
    }
}

// Grids of pseudo-random values: continuous ones, and ones drawn among the edge values (the iso
// level, +-0, subnormals, the smallest normals, infinities, NaN), in every proportion.
void checkRandom(Comparison& comparison) {
    using limits = std::numeric_limits<float>;
    const std::array<float, 14> edges = {
        0.0f,          -0.0f,         limits::denorm_min(), -limits::denorm_min(),
        limits::min(), -limits::min(), 1.0f,                -1.0f,
        0.5f,          -0.25f,        limits::infinity(),   -limits::infinity(),
        limits::max(), limits::quiet_NaN()};
    for (std::uint64_t seed = 0; seed < 40; ++seed) {
        Draws draws(seed);
        comparison.check(gridFrom([&draws](int, int, int) { return draws.unit(); }),
                         static_cast<double>(seed));
        const std::uint64_t share = seed % 8; // in eighths: 0 continuous only, 7 mostly edges
        comparison.check(gridFrom([&](int, int, int) {
                             if ((draws.next() & 7U) < share) {
                                 return edges[draws.next() % edges.size()];
                             }
                             return draws.unit();
                         }),
                         static_cast<double>(seed));
    }
}

void checkSynthetic() {
    Comparison comparison("synthetic grids");
    using limits = std::numeric_limits<float>;
    for (const float fill : {-1.0f, 1.0f, 0.0f, -0.0f, limits::denorm_min(), -limits::denorm_min(),
                             limits::infinity(), -limits::infinity(), limits::quiet_NaN()}) {
        comparison.check(DensityGrid(fill), static_cast<double>(fill));
    }
    // One solid or one air sample in a grid of the other kind, at every sample of a corner and
    // of the middle of the grid: every case of a cell with a single corner apart.
    for (const int at : {0, 1, 8, kGridSamples - 2, kGridSamples - 1}) {
        for (const float apart : {-1.0f, -0.0f, 0.0f, -limits::denorm_min()}) {
            DensityGrid grid = DensityGrid(apart < 0.0f ? 1.0f : -1.0f);
            grid.set(at, at, at, apart);
            comparison.check(grid, at);
        }
    }
    // Every case of a cell: the 8 corners of the cell (8, 8, 8) taking the 256 sign patterns.
    for (unsigned cube = 0; cube < 256; ++cube) {
        DensityGrid grid = DensityGrid(1.0f);
        for (int corner = 0; corner < 8; ++corner) {
            const bool solid = ((cube >> corner) & 1U) != 0;
            grid.set(8 + (corner & 1), 8 + ((corner >> 1) & 1), 8 + ((corner >> 2) & 1),
                     solid ? -0.5f - 0.0625f * static_cast<float>(corner) : 0.0f);
        }
        comparison.check(grid, cube);
    }
    checkShapes(comparison);
    checkRandom(comparison);
    TEST_CHECK(comparison.meshed() > 300);
}

} // namespace

int main() {
    checkWorlds();
    checkCaves();
    checkSynthetic();
    return test::result();
}

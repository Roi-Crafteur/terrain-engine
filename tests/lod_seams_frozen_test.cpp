// The stitching of the seams between LODs against its frozen reference of commit 5e96585 (tests/
// frozen/frozen_stitch.hpp, meshed by marchingCubes). On the grids of the natural relief (a
// mountain, the forests of the center of the tests, the coast at the origin and the corner of
// the world, for the default world and a planet; LOD 0 to 3; every mask of seams), the stitched
// grids and their meshes are the same bit for bit: the tiles without caves nor edits keep their
// bits. On the grids of the caves (the details of LOD 0 to 2 around cave entrances) and of
// craters dug across the sides, the vertices that change all lie on the seams: positions within
// one cell of a stitched side, normals within two (central differences); the counts and the
// largest move along an edge are printed.
#include "frozen/frozen_stitch.hpp"

#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/heightfield_density.hpp"
#include "terrain/meshing/marching_cubes.hpp"
#include "terrain/streaming/cave_tiles.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <tuple>
#include <vector>

namespace {

using terrain::DensityGrid;
using terrain::kGridSamples;
using terrain::Mesh;
using terrain::Vec3f;

constexpr auto kLast = static_cast<float>(kGridSamples - 1);

// Masks of seams: none, faces, corners, and all of them.
constexpr std::array<unsigned, 6> kSeams = {
    0U,
    terrain::kSeamNegX | terrain::kSeamPosZ,
    terrain::kSeamPosX | terrain::kSeamNegZ | terrain::kSeamPosXNegZ,
    terrain::kSeamNegXNegZ | terrain::kSeamNegXPosZ,
    terrain::kSeamPosXPosZ,
    0xFFU,
};

bool sameBits(const DensityGrid& a, const DensityGrid& b) {
    for (int k = 0; k < kGridSamples; ++k) {
        for (int j = 0; j < kGridSamples; ++j) {
            for (int i = 0; i < kGridSamples; ++i) {
                const float x = a.at(i, j, k);
                const float y = b.at(i, j, k);
                if (std::memcmp(&x, &y, sizeof x) != 0) {
                    return false;
                }
            }
        }
    }
    return true;
}

// Distance of a vertex, in cells, to the nearest stitched face of the mask.
float seamDistance(Vec3f v, unsigned seams) {
    float nearest = 2.0f * kLast;
    const std::array<std::tuple<unsigned, float>, 4> faces = {
        {{terrain::kSeamNegX, v.x}, {terrain::kSeamPosX, kLast - v.x},
         {terrain::kSeamNegZ, v.z}, {terrain::kSeamPosZ, kLast - v.z}}};
    for (const auto& [seam, distance] : faces) {
        if ((seams & seam) != 0) {
            nearest = std::min(nearest, distance);
        }
    }
    return nearest;
}

using Vertex = std::array<float, 6>;

// The vertices of the mesh farther than `beyond` cells from the stitched faces, sorted: their
// positions, with their normals when `normals`.
std::vector<Vertex> verticesBeyond(const Mesh& mesh, unsigned seams, float beyond, bool normals) {
    std::vector<Vertex> kept;
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const Vec3f v = mesh.vertices[i];
        const Vec3f n = normals ? mesh.normals[i] : Vec3f{};
        if (seamDistance(v, seams) > beyond) {
            kept.push_back({v.x, v.y, v.z, n.x, n.y, n.z});
        }
    }
    std::sort(kept.begin(), kept.end());
    return kept;
}

struct Tally {
    int grids = 0;
    int meshed = 0;
    int differing = 0;     // grids whose mesh changed
    int changed = 0;       // vertices of the new meshes absent from the old ones
    float largestMove = 0; // along an edge of a stitched face, cells
    float farthest = 0;    // largest distance of a changed vertex to the stitched faces, cells
    bool local = true;     // every change within the seams
};

// The largest move of the vertices on a stitched face lying on the same edge of the grid in
// both meshes (the same two whole coordinates), and the farthest changed vertex from the faces.
void measureMoves(const Mesh& before, const Mesh& after, unsigned seams, Tally& tally) {
    std::map<std::array<float, 3>, std::vector<Vec3f>> byEdge;
    const auto edgeKey = [](Vec3f v) {
        const auto whole = [](float c) { return std::floor(c) == c ? c : std::floor(c) + 0.5f; };
        return std::array<float, 3>{whole(v.x), whole(v.y), whole(v.z)};
    };
    for (const Vec3f& v : before.vertices) {
        byEdge[edgeKey(v)].push_back(v);
    }
    for (const Vec3f& v : after.vertices) {
        const auto same = [&v](Vec3f w) { return w == v; };
        const std::vector<Vec3f>& candidates = byEdge[edgeKey(v)];
        if (std::any_of(candidates.begin(), candidates.end(), same)) {
            continue;
        }
        ++tally.changed;
        tally.farthest = std::max(tally.farthest, seamDistance(v, seams));
        for (const Vec3f& w : candidates) {
            const float move = std::abs(w.x - v.x) + std::abs(w.y - v.y) + std::abs(w.z - v.z);
            tally.largestMove = std::max(tally.largestMove, move);
        }
    }
}

// The old and the new stitching of the grid with each mask: on natural grids the same bits;
// elsewhere every change within one cell of a stitched face (two for the normals).
void compare(const DensityGrid& grid, bool natural, Tally& tally) {
    for (const unsigned seams : kSeams) {
        DensityGrid before = grid;
        frozen::stitchSeams(before, seams);
        DensityGrid after = grid;
        terrain::stitchSeams(after, seams);
        const Mesh oldMesh = terrain::marchingCubes(before);
        const Mesh newMesh = terrain::stitchedMesh(after, seams);
        ++tally.grids;
        tally.meshed += newMesh.indices.empty() ? 0 : 1;
        if (natural) {
            tally.local = tally.local && sameBits(before, after) && newMesh == oldMesh;
            continue;
        }
        tally.differing += newMesh == oldMesh ? 0 : 1;
        measureMoves(oldMesh, newMesh, seams, tally);
        tally.local = tally.local &&
                      verticesBeyond(oldMesh, seams, 1.0f, false) ==
                          verticesBeyond(newMesh, seams, 1.0f, false) &&
                      verticesBeyond(oldMesh, seams, 2.0f, true) ==
                          verticesBeyond(newMesh, seams, 2.0f, true);
    }
}

void report(const char* name, const Tally& t) {
    std::printf("%-28s %4d grids (%4d meshed), %3d meshes changed: %5d vertices changed, largest "
                "move %.3f cell along an edge, the farthest %.3f cell from a stitched face; "
                "changes within the seams: %s\n",
                name, t.grids, t.meshed, t.differing, t.changed, t.largestMove, t.farthest,
                t.local ? "yes" : "no");
    TEST_CHECK(t.local);
}

// The grid of density(x, y, z) at (x0, y0, z0) + (i, j, k) * spacing.
DensityGrid gridOf(const std::function<double(double, double, double)>& density, double x0,
                   double y0, double z0, double spacing) {
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

// The levels of the chunk at the spacing of each LOD around the ground at its corner: the one
// holding the ground, the one below and the one above.
void checkRelief(const terrain::Heightfield& field, terrain::ChunkCoord chunk, Tally& tally) {
    const terrain::HeightfieldDensity density(field);
    const double x0 = chunk.x * terrain::kChunkSizeMeters;
    const double z0 = chunk.z * terrain::kChunkSizeMeters;
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        const double spacing = terrain::tileCellMeters(lod);
        const double height = terrain::kChunkCells * spacing;
        const double ground = std::floor(field.heightAt(x0, z0) / height) * height;
        for (const double y0 : {ground - height, ground, ground + height}) {
            compare(gridOf(density, x0, y0, z0, spacing), true, tally);
        }
    }
}

void testRelief() {
    Tally tally;
    terrain::HeightfieldParams planet;
    planet.macroContinent = 0.25f;
    planet.plates.amplitude = 0.5f;
    const terrain::ChunkCoord count = terrain::worldChunks(terrain::WorldSize{});
    for (const terrain::HeightfieldParams& params : {terrain::HeightfieldParams{}, planet}) {
        const terrain::Heightfield field(streaming_test::kSeed, params);
        checkRelief(field, streaming_test::mountainChunk(field), tally);
        checkRelief(field, {streaming_test::kCenterChunkX, streaming_test::kCenterChunkZ},
                    tally);
        checkRelief(field, {0, 0}, tally);
        checkRelief(field, {count.x / 2 - 1, count.z / 2 - 1}, tally);
    }
    report("natural relief, same bits", tally);
    TEST_CHECK(tally.meshed > 100);
}

// The carved density of the grid at (x0, y0, z0) + (i, j, k) * spacing with the detail of a
// LOD, as marching_cubes_frozen_test samples it.
DensityGrid caveGrid(const terrain::TerrainTiles& tiles, int lod, double x0, double y0, double z0) {
    const double spacing = terrain::tileCellMeters(lod);
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

// Grids of the details of LOD 0 to 2 around the bottoms of the first cave entrances near the
// center, where the entrance opens into its chamber.
void testCaves() {
    const terrain::TerrainTiles tiles(streaming_test::kSeed, {}, {}, terrain::CaveParams{});
    const std::vector<terrain::CaveEntrance> entrances = streaming_test::entrancesNearOrigin(tiles);
    Tally tally;
    for (std::size_t n = 0; n < entrances.size() && n < 6; ++n) {
        const terrain::CaveEntrance& e = entrances[n];
        for (int lod = 0; lod < terrain::kMaxLod; ++lod) {
            const double half = terrain::tileSizeMeters(lod) / 2.0;
            compare(caveGrid(tiles, lod, std::floor(e.bottomX - half), std::floor(e.bottomY - half),
                             std::floor(e.bottomZ - half)),
                    false, tally);
        }
    }
    report("caves", tally);
    TEST_CHECK(tally.differing > 0);
}

// Craters of several radii centered on the middle of each side of the grids of LOD 0 to 2 at
// the ground of a mountain and of the forests of the center: the relief without the spheres.
void testCraters() {
    const terrain::Heightfield field(streaming_test::kSeed);
    const terrain::HeightfieldDensity relief(field);
    Tally tally;
    for (const terrain::ChunkCoord chunk :
         {streaming_test::mountainChunk(field),
          terrain::ChunkCoord{streaming_test::kCenterChunkX, streaming_test::kCenterChunkZ}}) {
        for (int lod = 0; lod < terrain::kMaxLod; ++lod) {
            const double spacing = terrain::tileCellMeters(lod);
            const double half = terrain::tileSizeMeters(lod) / 2.0;
            const double x0 = chunk.x * terrain::kChunkSizeMeters;
            const double z0 = chunk.z * terrain::kChunkSizeMeters;
            for (const std::array<double, 2>& place :
                 {std::array<double, 2>{x0, z0 + half},
                  {x0 + 2.0 * half, z0 + half},
                  {x0 + half, z0},
                  {x0 + half, z0 + 2.0 * half}}) {
                const double cx = place[0];
                const double cz = place[1];
                const double cy = std::floor(field.heightAt(cx, cz));
                for (const double radius : {2.3, 3.7, 6.1}) {
                    const double r = radius * spacing;
                    const auto crater = [&](double x, double y, double z) {
                        const double dx = x - cx;
                        const double dy = y - cy;
                        const double dz = z - cz;
                        const double inside = r - std::sqrt(dx * dx + dy * dy + dz * dz);
                        return std::max(relief(x, y, z), inside);
                    };
                    compare(gridOf(crater, x0, cy - half, z0, spacing), false, tally);
                }
            }
        }
    }
    report("craters", tally);
    TEST_CHECK(tally.differing > 0);
}

} // namespace

int main() {
    testRelief();
    testCaves();
    testCraters();
    return test::result();
}

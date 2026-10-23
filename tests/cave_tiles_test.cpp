// Streamed tiles with caves: deterministic, the world without caves and the far LOD unchanged,
// the Marching Cubes integration (vertices on the carved isosurface, normals toward the air,
// closed meshes holding the whole cave band, cave surfaces only below the roof), the plants
// dropped over the entrances and the density of the lattice. Seams: cave_seams_test.cpp.
// On the legacy relief (legacy_relief_support.hpp): its places, around the center of the tests,
// were chosen in that world; the caves of the default world are tested by
// caves_default_world_test.
#include "terrain/generation/cave_entrances.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/streaming/cave_tiles.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "legacy_relief_support.hpp"
#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

namespace {

using terrain::CaveEntrance;
using terrain::ChunkCoord;
using terrain::TerrainTiles;
using terrain::TileData;
using terrain::TileKey;

const terrain::CaveParams kCaves{};

// Carved density of the world at a point of space, with the detail of a LOD.
float carvedAt(const TerrainTiles& tiles, double x, double y, double z, int lod = 0) {
    return terrain::caveDensityAt(*tiles.caves(), tiles.caveGround(),
                                  terrain::caveDetailForLod(lod), x, y, z);
}

// The same tiles from two worlds with caves, LOD 0 to 3; the far LOD 3 is the tile of the
// world without caves, the others are not where the caves reach the tile.
void testDeterministic(const TerrainTiles& tiles, ChunkCoord chunk) {
    const TerrainTiles again(streaming_test::kSeed, test_relief::legacyRelief(), {}, kCaves);
    const TerrainTiles plain(streaming_test::kSeed, test_relief::legacyRelief());
    const TerrainTiles other(streaming_test::kSeed + 1, test_relief::legacyRelief(), {}, kCaves);
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        const terrain::TileRequest request{terrain::tileContaining(chunk, lod),
                                           terrain::kSeamNegX | terrain::kSeamPosZ};
        const TileData data = tiles.generate(request);
        TEST_CHECK(!data.mesh.indices.empty());
        TEST_CHECK(data == tiles.generate(request));
        TEST_CHECK(data == again.generate(request));
        const bool same = data == plain.generate(request);
        TEST_CHECK(lod == terrain::kMaxLod ? same : !same);
        TEST_CHECK(lod == terrain::kMaxLod || data.mesh != other.generate(request).mesh);
    }
}

// Indices of the vertices welded by exact position: the levels of a tile share the vertices of
// their boundary planes, at the same positions.
std::vector<std::size_t> weld(const terrain::Mesh& mesh) {
    std::map<std::tuple<float, float, float>, std::size_t> ids;
    std::vector<std::size_t> welded;
    welded.reserve(mesh.vertices.size());
    for (const terrain::Vec3f& v : mesh.vertices) {
        welded.push_back(ids.try_emplace({v.x, v.y, v.z}, ids.size()).first->second);
    }
    return welded;
}

// Every edge of the welded mesh is shared by exactly two triangles, except on the sides of the
// tile, where its neighbors take over: the mesh is closed, so no cave is cut by a level that
// is not meshed.
bool closedInsideTile(const TileData& data, double side) {
    const std::vector<std::size_t> welded = weld(data.mesh);
    std::map<std::pair<std::size_t, std::size_t>, int> edges;
    const std::vector<std::uint32_t>& indices = data.mesh.indices;
    for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
        for (std::size_t e = 0; e < 3; ++e) {
            const std::size_t a = welded[indices[t + e]];
            const std::size_t b = welded[indices[t + (e + 1) % 3]];
            ++edges[{std::min(a, b), std::max(a, b)}];
        }
    }
    const auto onSide = [&](std::uint32_t index) {
        const terrain::Vec3f v = data.mesh.vertices[index];
        return v.x == 0.0f || v.z == 0.0f || v.x == side || v.z == side;
    };
    std::vector<std::uint32_t> anyVertex(welded.size());
    for (std::size_t i = 0; i < welded.size(); ++i) {
        anyVertex[welded[i]] = static_cast<std::uint32_t>(i);
    }
    return std::all_of(edges.begin(), edges.end(), [&](const auto& edge) {
        return edge.second == 2 ||
               (onSide(anyVertex[edge.first.first]) && onSide(anyVertex[edge.first.second]));
    });
}

struct MeshChecks {
    std::size_t vertices = 0;
    // The carved density changes sign along the normal within one cell of the vertex, and
    // within half a cell.
    std::size_t withinCell = 0;
    std::size_t withinHalfCell = 0;
    std::size_t outward = 0; // the density grows along the normal: it points toward the air
    std::size_t caveVertices = 0;
    std::size_t inRoof = 0; // between the ground and the minimum roof, away from the entrances
};

// True if the density changes sign between two of the samples every reach / 4 along the normal
// over [-reach, reach].
template <class Along>
bool signChange(const Along& along, double reach) {
    bool solid = false;
    bool air = false;
    for (int step = -4; step <= 4; ++step) {
        (along(reach * step / 4.0) < 0.0f ? solid : air) = true;
    }
    return solid && air;
}

// Carved density of the world around a tile, with the detail of its LOD: the same values as
// carvedAt, from one sampler over the tile and its surroundings.
class TileProbe {
public:
    TileProbe(const TerrainTiles& tiles, const TileData& data)
        : tiles_(&tiles), sampler_(*tiles.caves(), tiles.caveGround(),
                                   terrain::caveDetailForLod(data.request.key.lod), boxOf(data)) {}

    [[nodiscard]] float operator()(double x, double y, double z) {
        const float ground = tiles_->heightfield().heightAt(x, z);
        const terrain::CaveColumn column = sampler_.column(x, z, ground);
        return sampler_.density(column, y, static_cast<float>(y - static_cast<double>(ground)));
    }

private:
    // The tile and its vertices, with a margin of two cells for the probes along the normals.
    static terrain::CaveBox boxOf(const TileData& data) {
        const double margin = 2.0 * terrain::tileCellMeters(data.request.key.lod);
        const double side = terrain::tileSizeMeters(data.request.key.lod);
        double top = data.originY;
        for (const terrain::Vec3f& v : data.mesh.vertices) {
            top = std::max(top, data.originY + v.y);
        }
        return {data.originX - margin, data.originY - margin, data.originZ - margin,
                data.originX + side + margin, top + margin, data.originZ + side + margin};
    }

    const TerrainTiles* tiles_;
    terrain::CaveSampler sampler_;
};

MeshChecks checkMesh(const TerrainTiles& tiles, const TileData& data,
                     const std::vector<CaveEntrance>& entrances) {
    const terrain::CaveParams& params = tiles.caves()->params();
    const double cell = terrain::tileCellMeters(data.request.key.lod);
    TileProbe probe(tiles, data);
    MeshChecks checks;
    for (std::size_t i = 0; i < data.mesh.vertices.size(); ++i) {
        const streaming_test::WorldPoint p = streaming_test::worldVertex(data, i);
        const terrain::Vec3f n = data.mesh.normals[i];
        const auto along = [&](double t) {
            return probe(p.x + t * n.x, p.y + t * n.y, p.z + t * n.z);
        };
        ++checks.vertices;
        checks.withinCell += signChange(along, cell) ? 1U : 0U;
        checks.withinHalfCell += signChange(along, cell / 2.0) ? 1U : 0U;
        checks.outward += along(cell / 4.0) > along(-cell / 4.0) ? 1U : 0U;
        const double depth = tiles.heightfield().heightAt(p.x, p.z) - p.y;
        checks.caveVertices += depth >= params.minRoof - params.blend / 4.0f ? 1U : 0U;
        const bool nearEntrance = terrain::CaveField::nearEntrance(
            p.x, p.z, entrances, terrain::kEntranceReachMargin);
        checks.inRoof += !nearEntrance && depth > 1.0 && depth < params.minRoof - 2.0 ? 1U : 0U;
    }
    return checks;
}

// LOD 0 tiles at the opening of an entrance and at its chamber, and LOD 1 and 2 tiles around
// them: Marching Cubes vertices lie on the carved isosurface with normals toward the air, the
// caves are meshed below the roof and never inside it, and every mesh is closed. Walls of air
// or rock thinner than a cell alias: a few vertices are only within one cell of the surface,
// and fewer than 0.5 % not even (a sliver of rock thinner than a quarter of a cell along the
// normal, missed by the probes).
void testMarchingCubes(const TerrainTiles& tiles, const CaveEntrance& entrance) {
    const std::vector<CaveEntrance> entrances = streaming_test::entrancesNearOrigin(tiles);
    const ChunkCoord chunks[] = {streaming_test::entranceChunk(entrance),
                                 terrain::worldToChunk(entrance.bottomX, entrance.bottomZ)};
    const terrain::CaveParams& params = tiles.caves()->params();
    for (const ChunkCoord chunk : chunks) {
        for (int lod = 0; lod < terrain::kMaxLod; ++lod) {
            const TileData data = tiles.generate({terrain::tileContaining(chunk, lod), 0});
            const MeshChecks checks = checkMesh(tiles, data, entrances);
            std::printf("LOD %d tile at (%d, %d): %zu vertices, %zu within a cell of the "
                        "surface, %zu within half a cell, %zu outward, %zu in caves, %zu in the "
                        "roof\n",
                        lod, chunk.x, chunk.z, checks.vertices, checks.withinCell,
                        checks.withinHalfCell, checks.outward, checks.caveVertices,
                        checks.inRoof);
            const double share = static_cast<double>(checks.vertices) * 0.98;
            TEST_CHECK(closedInsideTile(data, terrain::tileSizeMeters(lod)));
            TEST_CHECK(data.originY <= data.minHeight - params.maxDepth);
            TEST_CHECK(checks.withinCell * 1000 >= checks.vertices * 995);
            TEST_CHECK(static_cast<double>(checks.withinHalfCell) >= share);
            TEST_CHECK(static_cast<double>(checks.outward) >= share);
            TEST_CHECK(checks.caveVertices > 0);
            TEST_CHECK(checks.inRoof == 0);
        }
    }
}

// An entrance whose opening chunk holds plants in the world without caves.
std::optional<CaveEntrance> entranceWithPlants(const TerrainTiles& plain,
                                               const std::vector<CaveEntrance>& entrances) {
    for (const CaveEntrance& entrance : entrances) {
        const TileData data = plain.generate({{streaming_test::entranceChunk(entrance), 0}, 0});
        const bool near = std::any_of(data.plants.begin(), data.plants.end(), [&](const auto& p) {
            return terrain::CaveField::nearEntrance(p.x, p.z, entrances, 2.0);
        });
        if (near) {
            return entrance;
        }
    }
    return std::nullopt;
}

// The plants of a LOD 0 tile are those of the world without caves, without the ones standing
// within 2 m of an entrance.
void testPlants(const TerrainTiles& tiles) {
    const TerrainTiles plain(streaming_test::kSeed, test_relief::legacyRelief());
    const std::vector<CaveEntrance> entrances = streaming_test::entrancesNearOrigin(tiles);
    const std::optional<CaveEntrance> entrance = entranceWithPlants(plain, entrances);
    TEST_CHECK(entrance.has_value());
    if (!entrance) {
        return;
    }
    const TileKey key{streaming_test::entranceChunk(*entrance), 0};
    const std::vector<terrain::Plant> carved = tiles.generate({key, 0}).plants;
    const std::vector<terrain::Plant> all = plain.generate({key, 0}).plants;
    std::vector<terrain::Plant> kept;
    for (const terrain::Plant& plant : all) {
        if (!terrain::CaveField::nearEntrance(plant.x, plant.z, entrances, 2.0)) {
            kept.push_back(plant);
        }
    }
    std::printf("plants: %zu without caves, %zu with caves\n", all.size(), carved.size());
    TEST_CHECK(carved == kept);
    TEST_CHECK(carved.size() < all.size());
}

// The density of the lattice is the carved density of LOD 0: inside the tube of an entrance
// below the ground, and y - height above the ground away from the entrances.
void testDensityAt(const TerrainTiles& tiles, const CaveEntrance& entrance) {
    bool matches = true;
    int carvedSamples = 0;
    const double midX = (entrance.topX + entrance.bottomX) / 2.0;
    const double midY = (entrance.topY + entrance.bottomY) / 2.0;
    const double midZ = (entrance.topZ + entrance.bottomZ) / 2.0;
    for (int dz = -3; dz <= 3; ++dz) {
        for (int dy = -3; dy <= 3; ++dy) {
            for (int dx = -3; dx <= 3; ++dx) {
                const auto x = static_cast<int>(std::lround(midX)) + dx;
                const auto y = static_cast<int>(std::lround(midY)) + dy;
                const auto z = static_cast<int>(std::lround(midZ)) + dz;
                const float density = tiles.densityAt(x, y, z);
                matches = matches && density == carvedAt(tiles, x, y, z);
                const float ground = tiles.heightfield().heightAt(x, z);
                carvedSamples += density >= 0.0f && static_cast<float>(y) < ground ? 1 : 0;
            }
        }
    }
    TEST_CHECK(matches);
    TEST_CHECK(carvedSamples > 0);
    const TerrainTiles plain(streaming_test::kSeed, test_relief::legacyRelief());
    const int x = 4096;
    const int z = -77;
    const auto y = static_cast<int>(std::ceil(tiles.heightfield().heightAt(x, z))) + 2;
    TEST_CHECK(tiles.densityAt(x, y, z) == plain.densityAt(x, y, z));
}

} // namespace

int main() {
    const TerrainTiles tiles(streaming_test::kSeed, test_relief::legacyRelief(), {}, kCaves);
    const std::vector<CaveEntrance> entrances = streaming_test::entrancesNearOrigin(tiles);
    TEST_CHECK(!entrances.empty());
    if (entrances.empty()) {
        return test::result();
    }
    std::printf("%zu entrances near the origin\n", entrances.size());
    const CaveEntrance& entrance = entrances.front();
    testDeterministic(tiles, streaming_test::entranceChunk(entrance));
    testMarchingCubes(tiles, entrance);
    testPlants(tiles);
    testDensityAt(tiles, entrance);
    return test::result();
}

// The meshes of the tiles by the K2 (phase 12A), as a client receives them: all the tiles of LOD 0
// to 3 it streams from the eyes of the views of the reviews (the two of the second stop, cap 215
// degrees from (416 087, -11 414 878) and (416 127, -11 414 935) toward (416 041, -11 414 812),
// and the view of the black slits from (416 080, -11 414 957)), with rock or of the relief alone:
// no opening within a tile (an edge of a single triangle off the sides of the tile,
// mesh_checks.hpp), every edge of two triangles at most, no triangle whose normals point against
// its winding (a client lights it as if seen from behind: black; the normals of every tile agree
// with their triangles, tile_normals.hpp), and on the side two tiles of LOD 0 share, the same
// normals on both (the light of a face goes on across the tiles), but for one vertex in 500 at
// most.
#include "terrain/io/mesh_checks.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <map>
#include <vector>

namespace {

using terrain::TileData;

constexpr std::array<std::array<double, 2>, 3> kEyes = {{{416'087.0, -11'414'878.0},
                                                         {416'127.0, -11'414'935.0},
                                                         {416'080.0, -11'414'957.4}}};
constexpr int kViewChunks = 53; // the tiles out to about 850 m, as the views drew them
// The tallest faces by the K2 and the foot of its highest wall (rock_seams_test).
constexpr std::array<std::array<double, 2>, 2> kFaces = {{{415'765.0, -11'414'542.0},
                                                          {416'017.0, -11'414'806.0}}};
constexpr float kSameNormal = 1e-4f;
// At most one shared vertex in kSharedShare apart: those whose normal the tile turned toward its
// triangles (rock_tile.cpp: on a fold as thin as a cell), which the tile sees on its side only.
constexpr std::size_t kSharedShare = 500;

struct Tally {
    std::size_t tiles = 0;
    std::size_t withRock = 0;
    std::size_t triangles = 0;
    std::size_t openEdges = 0;
    std::size_t nonManifold = 0;
    std::size_t inverted = 0;       // in the tiles with rock
    std::size_t reliefInverted = 0; // in the tiles of the relief alone
    std::array<std::size_t, 4> lods{};
};

void add(const TileData& t, Tally& tally) {
    const terrain::MeshCheck c = terrain::checkTileMesh(
        t.mesh, static_cast<float>(terrain::tileSizeMeters(t.request.key.lod)));
    ++tally.tiles;
    ++tally.lods[static_cast<std::size_t>(t.request.key.lod)];
    tally.withRock += t.rock.empty() ? 0 : 1;
    tally.triangles += c.triangles;
    tally.openEdges += c.openEdges;
    tally.nonManifold += c.nonManifoldEdges;
    (t.rock.empty() ? tally.reliefInverted : tally.inverted) += c.inverted;
}

// The tiles a client streams from each eye (LOD 0 to 3, with their seams).
void testViews(const terrain::TerrainTiles& tiles) {
    Tally tally;
    for (const auto& [x, z] : kEyes) {
        terrain::LodParams lod;
        lod.viewDistance = kViewChunks;
        for (const terrain::TileRequest& r :
             terrain::tileRequests(terrain::selectTiles(terrain::worldToChunk(x, z), lod))) {
            add(tiles.generate(r), tally);
        }
    }
    std::printf("the views of the reviews: %zu tiles (LOD 0 to 3: %zu, %zu, %zu, %zu; %zu with "
                "rock), %zu triangles; %zu open edges, %zu edges of more than two triangles, %zu "
                "inverted triangles with rock, %zu on the relief alone\n",
                tally.tiles, tally.lods[0], tally.lods[1], tally.lods[2], tally.lods[3],
                tally.withRock, tally.triangles, tally.openEdges, tally.nonManifold,
                tally.inverted, tally.reliefInverted);
    TEST_CHECK(tally.withRock >= 20);
    TEST_CHECK(tally.lods[3] >= 100);
    TEST_CHECK(tally.openEdges == 0 && tally.nonManifold == 0);
    TEST_CHECK(tally.inverted == 0 && tally.reliefInverted == 0);
}

// The vertices of a mesh on the plane x = at (alongX) or z = at, by their positions.
std::map<std::array<float, 3>, terrain::Vec3f> sideOf(const TileData& t, bool alongX, double at) {
    std::map<std::array<float, 3>, terrain::Vec3f> out;
    for (std::size_t i = 0; i < t.mesh.vertices.size(); ++i) {
        const terrain::Vec3f& v = t.mesh.vertices[i];
        const auto x = static_cast<float>(t.originX + static_cast<double>(v.x));
        const auto y = static_cast<float>(t.originY + static_cast<double>(v.y));
        const auto z = static_cast<float>(t.originZ + static_cast<double>(v.z));
        if ((alongX ? t.originX + static_cast<double>(v.x) : t.originZ + static_cast<double>(v.z)) ==
            at) {
            out[{x, y, z}] = t.mesh.normals[i];
        }
    }
    return out;
}

// Two LOD 0 tiles side by side around the tallest faces: the vertices of the side they share
// carry the same normals.
void testSameNormalsAcrossSides(const terrain::TerrainTiles& tiles) {
    std::size_t shared = 0;
    std::size_t differ = 0;
    for (const auto& [x, z] : kFaces) {
        const terrain::ChunkCoord c = terrain::worldToChunk(x, z);
        for (int dz = -2; dz <= 2; ++dz) {
            for (int dx = -2; dx <= 2; ++dx) {
                for (const bool alongX : {true, false}) {
                    const TileData a = tiles.generate({{{c.x + dx, c.z + dz}, 0}, 0});
                    const TileData b = tiles.generate(
                        {{{c.x + dx + (alongX ? 1 : 0), c.z + dz + (alongX ? 0 : 1)}, 0}, 0});
                    if (a.rock.empty() || b.rock.empty()) {
                        continue;
                    }
                    const double at = alongX ? b.originX : b.originZ;
                    const auto onA = sideOf(a, alongX, at);
                    for (const auto& [position, n] : sideOf(b, alongX, at)) {
                        const auto found = onA.find(position);
                        if (found == onA.end()) {
                            continue;
                        }
                        const terrain::Vec3f& m = found->second;
                        const float gap = std::max(
                            {std::abs(n.x - m.x), std::abs(n.y - m.y), std::abs(n.z - m.z)});
                        differ += gap > kSameNormal ? 1 : 0;
                        ++shared;
                    }
                }
            }
        }
    }
    std::printf("the sides of LOD 0 tiles with rock by the K2: %zu shared vertices, %zu with "
                "normals apart by more than %.0e\n",
                shared, differ, static_cast<double>(kSameNormal));
    TEST_CHECK(shared > 1'000);
    TEST_CHECK(differ * kSharedShare <= shared);
}

} // namespace

int main() {
    const terrain::TerrainTiles tiles(terrain::kDefaultWorldSeed);
    testViews(tiles);
    testSameNormalsAcrossSides(tiles);
    return test::result();
}

// Lava in the streamed world (Phase 8C-2), in a world of volcanic regions on the heightfield:
// the entrances stay above the lava of the cells they cross; the tiles of LOD 0 and LOD 1 over a
// lava lake mesh it, LOD 2 none, and the world without cave regions none at all; the walls
// painted magma stand within magmaHeight over the lava of their cell.
#include "terrain/generation/aquifers.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/caves.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "cave_galleries_test_support.hpp"
#include "caves_test_support.hpp"
#include "legacy_relief_support.hpp"
#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

namespace {

using caves_test::kSeed;
using terrain::AquiferCache;
using terrain::AquiferCell;
using terrain::CaveColumn;
using terrain::CaveDistrictType;
using terrain::CaveEntrance;
using terrain::CaveField;
using terrain::CaveGround;
using terrain::CaveSampler;
using terrain::TerrainTiles;
using terrain::TileData;

struct LavaPoint {
    double x;
    double z;
};

// Points where carved air lies under the lava level of a lava cell, one per cell, on land.
std::vector<LavaPoint> lavaPoints(const CaveField& field, const CaveGround& ground, int count) {
    std::vector<LavaPoint> points;
    const double size = field.params().aquifers.cellSize;
    for (int n = 0; n < 900 && static_cast<int>(points.size()) < count; ++n) {
        const AquiferCell cell = field.aquifer(n % 30 - 15, n / 30 - 15, ground);
        if (!cell.lava || ground.height(cell.siteX, cell.siteZ) < 20.0f) {
            continue;
        }
        const double level = static_cast<double>(cell.lavaLevel);
        CaveSampler sampler(field, ground, terrain::CaveDetail{},
                            {cell.siteX - size, level - 12.0, cell.siteZ - size,
                             cell.siteX + size, level + 1.0, cell.siteZ + size});
        bool found = false;
        for (double z = cell.siteZ - 96.0; z <= cell.siteZ + 96.0 && !found; z += 4.0) {
            for (double x = cell.siteX - 96.0; x <= cell.siteX + 96.0 && !found; x += 4.0) {
                const CaveColumn c = sampler.column(x, z, ground.height(x, z));
                const double y = static_cast<double>(c.lavaLevel) - 1.0;
                const float rock = caves_test::terrainDensity(y, c.ground);
                found = c.lavaLevel == cell.lavaLevel &&
                        sampler.lavaDensity(c, y, rock, sampler.density(c, y, rock)) < 0.0f;
                if (found) {
                    points.push_back({x, z});
                }
            }
        }
    }
    return points;
}

// Along the axis of the entrances around the lava points, every 1 %: the tube (or the chamber,
// at the bottom) stays entranceMargin over the lava of the cell under it.
void testEntrances(const TerrainTiles& tiles, const std::vector<LavaPoint>& points) {
    const CaveField& field = *tiles.caves();
    const CaveGround& ground = tiles.caveGround();
    AquiferCache cache(field, ground);
    const double margin = static_cast<double>(field.params().aquifers.entranceMargin);
    int entrances = 0;
    int overLava = 0;
    int low = 0;
    for (const LavaPoint& p : points) {
        for (const CaveEntrance& e :
             field.entrancesNear(p.x - 300.0, p.z - 300.0, p.x + 300.0, p.z + 300.0, ground,
                                 cache)) {
            ++entrances;
            bool over = false;
            for (int s = 0; s <= 100; ++s) {
                const double t = s / 100.0;
                const double x = e.topX + t * (e.bottomX - e.topX);
                const double y = e.topY + t * (e.bottomY - e.topY);
                const double z = e.topZ + t * (e.bottomZ - e.topZ);
                const AquiferCell& cell = cache.nearest(x, z);
                if (!cell.lava) {
                    continue;
                }
                over = true;
                const double radius = s == 100 ? e.chamberRadius : e.tubeRadius;
                low += y - radius < static_cast<double>(cell.lavaLevel) + margin - 1e-3 ? 1 : 0;
            }
            overLava += over ? 1 : 0;
        }
    }
    std::printf("entrances: %d around the lava, %d over lava cells, %d axis samples too low\n",
                entrances, overLava, low);
    TEST_CHECK(overLava > 0);
    TEST_CHECK(low == 0);
}

// The tiles over the lava points: lava meshed at LOD 0 and LOD 1, none at LOD 2; magma walls
// within magmaHeight over the lava of their cell, under the ground; none of it without regions.
void testTiles(const TerrainTiles& tiles, const std::vector<LavaPoint>& points) {
    const CaveField& field = *tiles.caves();
    AquiferCache cache(field, tiles.caveGround());
    const TerrainTiles plain(kSeed, test_relief::legacyRelief(), {}, terrain::CaveParams{});
    const float magmaHeight = field.params().materials.magmaHeight;
    std::array<int, 3> withLava{};
    int magma = 0;
    int strayMagma = 0;
    int plainLava = 0;
    for (const LavaPoint& p : points) {
        const terrain::ChunkCoord chunk = terrain::worldToChunk(p.x, p.z);
        for (int lod = 0; lod < 3; ++lod) {
            const TileData data = tiles.generate({terrain::tileContaining(chunk, lod), 0});
            withLava[static_cast<std::size_t>(lod)] += data.lava.vertices.empty() ? 0 : 1;
            if (lod != 0) {
                continue;
            }
            for (std::size_t n = 0; n < data.mesh.vertices.size(); ++n) {
                if (data.materials[n] != terrain::CaveMaterial::Magma) {
                    continue;
                }
                ++magma;
                const streaming_test::WorldPoint w = streaming_test::worldVertex(data, n);
                const AquiferCell& cell = cache.nearest(w.x, w.z);
                const bool near =
                    cell.lava && w.y - static_cast<double>(cell.lavaLevel) <=
                                     static_cast<double>(magmaHeight) + 1e-3;
                strayMagma += near ? 0 : 1;
            }
        }
        const TileData none = plain.generate({terrain::tileContaining(chunk, 0), 0});
        plainLava += none.lava.vertices.empty() ? 0 : 1;
    }
    std::printf("tiles: %zu lava points, with lava at LOD 0 %d, LOD 1 %d, LOD 2 %d; %d magma "
                "vertices (%d away from the lava); %d tiles with lava without regions\n",
                points.size(), withLava[0], withLava[1], withLava[2], magma, strayMagma,
                plainLava);
    TEST_CHECK(withLava[0] == static_cast<int>(points.size()));
    TEST_CHECK(withLava[1] > 0 && withLava[2] == 0);
    TEST_CHECK(magma > 0 && strayMagma == 0 && plainLava == 0);
}

} // namespace

int main() {
    const TerrainTiles tiles(kSeed, test_relief::legacyRelief(), {},
                             galleries_test::uniformParams(CaveDistrictType::Volcanic));
    const std::vector<LavaPoint> points = lavaPoints(*tiles.caves(), tiles.caveGround(), 4);
    TEST_CHECK(points.size() == 4);
    testEntrances(tiles, points);
    testTiles(tiles, points);
    return test::result();
}

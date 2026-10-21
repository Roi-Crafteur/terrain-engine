// Rivers in the streamed tiles (Phase 8C-2): in the world of key 5 (the heightfield with the
// default cave regions), the tiles of LOD 0 and LOD 1 over a river hold its water, LOD 2 none;
// two neighbor tiles of the same LOD put the same water vertices on their shared side, so that
// the river crosses the chunks without a seam; generating a tile twice gives the same water.
#include "terrain/generation/aquifers.hpp"
#include "terrain/generation/caves.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "caves_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <set>
#include <tuple>

namespace {

using caves_test::kSeed;
using terrain::AquiferCache;
using terrain::CaveColumn;
using terrain::CaveDistrictType;
using terrain::CaveField;
using terrain::TerrainTiles;
using terrain::TileData;
using terrain::TileRequest;

// A point on the axis of a gallery whose river is at least 0.6 m deep, around the sites of the
// Rivers regions on land, from the cells z = 0 northward (the rivers of the first region south of
// them run too thin for the lattice of LOD 1).
std::optional<std::array<double, 2>> riverPoint(const TerrainTiles& tiles) {
    const CaveField& field = *tiles.caves();
    const terrain::CaveGround& ground = tiles.caveGround();
    AquiferCache cache(field, ground);
    const double minDepth = static_cast<double>(field.params().rivers.minDepth);
    for (int n = 0; n < 625; ++n) {
        const terrain::CaveDistrict d = field.district(n % 25 - 12, n / 25, ground);
        if (d.type != CaveDistrictType::Rivers || ground.height(d.siteX, d.siteZ) < 5.0f) {
            continue;
        }
        for (double z = d.siteZ - 200.0; z < d.siteZ + 200.0; z += 2.0) {
            for (double x = d.siteX - 200.0; x < d.siteX + 200.0; x += 0.5) {
                const CaveColumn c =
                    field.column(x, z, ground.height(x, z), ground, {}, &cache, false);
                const double depth = static_cast<double>(c.riverLevel - c.galleryFloor) +
                                     minDepth;
                if (c.galleryDistance < 0.3f && c.riverLevel > terrain::kDryLevel &&
                    depth >= 0.6) {
                    return std::array<double, 2>{x, z};
                }
            }
        }
    }
    return std::nullopt;
}

using Vertex = std::tuple<long long, long long, long long>;

// Water vertices of a tile lying on the world plane x = side, in tenths of millimeters.
std::set<Vertex> waterOnSide(const TileData& data, double side) {
    std::set<Vertex> result;
    for (const terrain::Vec3f& v : data.water.vertices) {
        const double x = data.originX + v.x;
        if (std::abs(x - side) > 1e-3) {
            continue;
        }
        const auto tenths = [](double value) { return std::llround(value * 1e4); };
        result.insert({tenths(x), tenths(data.originY + v.y), tenths(data.originZ + v.z)});
    }
    return result;
}

void testRiverTiles() {
    terrain::CaveParams params;
    params.districts = terrain::CaveDistrictParams{};
    const TerrainTiles tiles(kSeed, {}, {}, params);
    const std::optional<std::array<double, 2>> point = riverPoint(tiles);
    TEST_CHECK(point.has_value());
    if (!point) {
        return;
    }
    const terrain::ChunkCoord chunk = terrain::worldToChunk((*point)[0], (*point)[1]);
    std::array<std::size_t, 3> water{};
    for (int lod = 0; lod < 3; ++lod) {
        const TileData data = tiles.generate({terrain::tileContaining(chunk, lod), 0});
        water[static_cast<std::size_t>(lod)] = data.water.vertices.size();
        TEST_CHECK(data == tiles.generate({terrain::tileContaining(chunk, lod), 0}));
    }
    // The river crosses the side x of the chunk or of one of its neighbors: the two tiles of
    // that side carry the same water vertices on it.
    std::size_t shared = 0;
    bool same = true;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const terrain::ChunkCoord west{chunk.x + dx, chunk.z + dz};
            const terrain::ChunkCoord east{west.x + 1, west.z};
            const TileData a = tiles.generate(TileRequest{{west, 0}, 0});
            const TileData b = tiles.generate(TileRequest{{east, 0}, 0});
            const double side = (west.x + 1) * terrain::kChunkSizeMeters;
            const std::set<Vertex> left = waterOnSide(a, side);
            same = same && left == waterOnSide(b, side);
            shared += left.size();
        }
    }
    std::printf("river tiles at (%.0f, %.0f): water vertices LOD 0 %zu, LOD 1 %zu, LOD 2 %zu; "
                "%zu shared on the sides of the chunks\n",
                (*point)[0], (*point)[1], water[0], water[1], water[2], shared);
    TEST_CHECK(water[0] > 0 && water[1] > 0 && water[2] == 0);
    TEST_CHECK(shared > 0);
    TEST_CHECK(same);
}

} // namespace

int main() {
    testRiverTiles();
    return test::result();
}

// Terrain tiles: deterministic generation, LOD 0 meshes on the heightfield, the lattice of each
// LOD, the vertical levels, plants on LOD 0 only and identical to placing them over a larger
// area, and the density of the lattice. Seams: lod_seams_test.cpp; edits: persistence_test.cpp.
#include "terrain/streaming/terrain_tiles.hpp"

#include "terrain/generation/terrain_surface.hpp"

#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using terrain::ChunkCoord;
using terrain::Plant;
using terrain::TerrainTiles;
using terrain::TileData;
using terrain::TileKey;

void testDeterministic() {
    const TerrainTiles a(streaming_test::kSeed);
    const TerrainTiles b(streaming_test::kSeed);
    const ChunkCoord m = streaming_test::mountainChunk(a.heightfield());
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        const terrain::TileRequest request{{m, lod}, terrain::kSeamNegX | terrain::kSeamPosZ};
        const TileData first = a.generate(request);
        TEST_CHECK(!first.mesh.indices.empty());
        TEST_CHECK(first == a.generate(request));
        TEST_CHECK(first == b.generate(request));
    }
    const TerrainTiles other(streaming_test::kSeed + 1);
    TEST_CHECK(other.generate({{m, 0}, 0}).mesh != a.generate({{m, 0}, 0}).mesh);
}

// LOD 0 vertices lie on the heightfield, within the ground range of the tile, and the tile
// origin is its lowest corner.
void testLod0FollowsHeightfield() {
    const TerrainTiles tiles(streaming_test::kSeed);
    const ChunkCoord m = streaming_test::mountainChunk(tiles.heightfield());
    const TileData data = tiles.generate({{m, 0}, 0});
    TEST_CHECK(data.originX == m.x * terrain::kChunkSizeMeters);
    TEST_CHECK(data.originZ == m.z * terrain::kChunkSizeMeters);
    TEST_CHECK(data.originY <= data.minHeight && data.minHeight < data.maxHeight);
    TEST_CHECK(data.mesh.normals.size() == data.mesh.vertices.size());
    bool onSurface = true;
    bool inRange = true;
    for (std::size_t i = 0; i < data.mesh.vertices.size(); ++i) {
        const streaming_test::WorldPoint p = streaming_test::worldVertex(data, i);
        const double h = tiles.heightfield().heightAt(p.x, p.z);
        // Linear interpolation along the horizontal edges of 1 m cells of a mountain slope.
        onSurface = onSurface && std::abs(p.y - h) <= 0.25;
        inRange = inRange && p.y >= data.minHeight - 1e-3 && p.y <= data.maxHeight + 1e-3;
    }
    TEST_CHECK(onSurface);
    TEST_CHECK(inRange);
}

// Each vertex of a LOD lies on a lattice line of its cells: two of its coordinates are
// multiples of tileCellMeters (Marching Cubes vertices lie on cell edges), and the tile spans
// tileSizeMeters. Coarser LODs hold fewer vertices per area.
void testLatticeOfEachLod() {
    const TerrainTiles tiles(streaming_test::kSeed);
    const ChunkCoord m = streaming_test::mountainChunk(tiles.heightfield());
    double previousDensity = 0.0;
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        const TileData data = tiles.generate({{m, lod}, 0});
        const double cell = terrain::tileCellMeters(lod);
        const double side = terrain::tileSizeMeters(lod);
        bool onLattice = true;
        bool inside = true;
        for (const terrain::Vec3f& v : data.mesh.vertices) {
            const auto multiple = [cell](double value) {
                return std::fmod(value, cell) == 0.0;
            };
            const int count = (multiple(v.x) ? 1 : 0) + (multiple(v.y) ? 1 : 0) +
                              (multiple(v.z) ? 1 : 0);
            onLattice = onLattice && count >= 2;
            inside = inside && v.x >= 0.0f && v.x <= side && v.z >= 0.0f && v.z <= side;
        }
        TEST_CHECK(onLattice);
        TEST_CHECK(inside);
        const double density = static_cast<double>(data.mesh.vertices.size()) / (side * side);
        TEST_CHECK(lod == 0 || density < previousDensity);
        previousDensity = density;
    }
}

// The levels of a tile cover its ground, even exactly on a level boundary.
void testLevelsCoverTheGround() {
    const TerrainTiles tiles(streaming_test::kSeed);
    const ChunkCoord m = streaming_test::mountainChunk(tiles.heightfield());
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        const TileData data = tiles.generate({{m, lod}, 0});
        const double level = terrain::tileSizeMeters(lod);
        TEST_CHECK(std::fmod(data.originY, level) == 0.0);
        float low = data.maxHeight;
        float high = data.minHeight;
        for (std::size_t i = 0; i < data.mesh.vertices.size(); ++i) {
            const auto y = static_cast<float>(streaming_test::worldVertex(data, i).y);
            low = std::min(low, y);
            high = std::max(high, y);
        }
        // The extreme columns of the ground are meshed.
        TEST_CHECK(std::abs(low - data.minHeight) < 1e-3f);
        TEST_CHECK(std::abs(high - data.maxHeight) < 1e-3f);
    }
}

bool plantOrder(const Plant& a, const Plant& b) {
    return std::tie(a.z, a.x) < std::tie(b.z, b.x);
}

// Plants of 2 x 2 LOD 0 tiles are exactly the plants of their union, placed by Ecology over a
// map on the same sample lattice: no seam between chunks. Coarser tiles hold none.
void testPlantsOnLod0Only() {
    const TerrainTiles tiles(streaming_test::kSeed);
    const ChunkCoord c = streaming_test::plantChunk(tiles, 3);
    std::vector<Plant> fromTiles;
    for (int dz = 0; dz < 2; ++dz) {
        for (int dx = 0; dx < 2; ++dx) {
            const TileData data = tiles.generate({{{c.x + dx, c.z + dz}, 0}, 0});
            fromTiles.insert(fromTiles.end(), data.plants.begin(), data.plants.end());
        }
    }
    TEST_CHECK(fromTiles.size() >= 3);

    const double x0 = c.x * terrain::kChunkSizeMeters;
    const double z0 = c.z * terrain::kChunkSizeMeters;
    const double side = 2.0 * terrain::kChunkSizeMeters;
    // Samples at x0 - 2 + 4 i, as the tiles.
    const terrain::HeightmapRegion region{x0 + side / 2.0, z0 + side / 2.0, 4.0,
                                          static_cast<int>(side / 4.0) + 2};
    const terrain::EcologyMap map =
        tiles.ecology().map(terrain::surfaceFromHeightfield(tiles.heightfield(), region));
    const terrain::ReliefHeight surface = [&tiles](double x, double z) {
        return tiles.heightfield().heightAt(x, z);
    };
    std::vector<Plant> direct =
        tiles.ecology().placePlants(map, surface, {x0, z0, x0 + side, z0 + side});
    std::sort(fromTiles.begin(), fromTiles.end(), plantOrder);
    std::sort(direct.begin(), direct.end(), plantOrder);
    TEST_CHECK(fromTiles == direct);

    for (int lod = 1; lod < terrain::kLodCount; ++lod) {
        TEST_CHECK(tiles.generate({terrain::tileContaining(c, lod), 0}).plants.empty());
    }
}

// The density of the lattice is the heightfield density without edits.
void testDensityAt() {
    const TerrainTiles tiles(streaming_test::kSeed);
    bool matches = true;
    for (const auto& [x, z] : {std::pair(0, 0), std::pair(-123, 456), std::pair(4096, -77)}) {
        const float h = tiles.heightfield().heightAt(x, z);
        for (const int dy : {-3, 0, 2}) {
            const int y = static_cast<int>(std::floor(h)) + dy;
            const auto expected = static_cast<float>(static_cast<double>(y) - h);
            matches = matches && tiles.densityAt(x, y, z) == expected;
        }
    }
    TEST_CHECK(matches);
}

} // namespace

int main() {
    testDeterministic();
    testLod0FollowsHeightfield();
    testLatticeOfEachLod();
    testLevelsCoverTheGround();
    testPlantsOnLod0Only();
    testDensityAt();
    return test::result();
}

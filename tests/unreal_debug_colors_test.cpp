// The colors of the vertices of the tiles in the view modes of the tools of the developers
// (Bridge/debug_colors.hpp): one hue per LOD, the classes of the slopes and the overhangs, the
// normals as colors, and the vertices off the heightmap: none on the natural relief of the
// small world at LOD 0 to 3, some on the walls of its caves and around an edit.
#include "debug_colors.hpp"

#include "streaming_test_support.hpp"
#include "test_assert.hpp"
#include "water_level_support.hpp"

#include "terrain/storage/volume_edit.hpp"

#include <cstdio>
#include <cmath>
#include <set>
#include <tuple>
#include <utility>

namespace {

using terrain::unreal::UnrealMesh;
using terrain::unreal::VertexColor;
using terrain::unreal::ViewMode;

bool sameColor(const VertexColor& a, const VertexColor& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

void testColors() {
    std::set<std::tuple<int, int, int>> hues;
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        const VertexColor c = terrain::unreal::lodColor(lod);
        hues.insert({c.r, c.g, c.b});
    }
    TEST_CHECK(hues.size() == 4);
    TEST_CHECK(sameColor(terrain::unreal::lodColor(9), terrain::unreal::lodColor(3)));
    using terrain::unreal::slopeColor;
    const auto atDegrees = [](double degrees) {
        const double a = degrees * 3.14159265358979 / 180.0;
        return terrain::unreal::UnrealVector3f{static_cast<float>(std::sin(a)), 0.0f,
                                               static_cast<float>(std::cos(a))};
    };
    std::set<std::tuple<int, int, int>> classes;
    for (const double degrees : {5.0, 20.0, 40.0, 50.0, 70.0, 85.0, 120.0}) {
        const VertexColor c = slopeColor(atDegrees(degrees));
        classes.insert({c.r, c.g, c.b});
    }
    TEST_CHECK(classes.size() == 7);
    TEST_CHECK(sameColor(slopeColor(atDegrees(1.0)), slopeColor(atDegrees(14.0))));
    TEST_CHECK(sameColor(slopeColor(atDegrees(95.0)), slopeColor({0.0f, 0.0f, -1.0f})));
    const VertexColor up = terrain::unreal::normalColor({0.0f, 0.0f, 1.0f});
    TEST_CHECK(up.r == 128 && up.g == 128 && up.b == 255);
    const VertexColor west = terrain::unreal::normalColor({-1.0f, 0.0f, 0.0f});
    TEST_CHECK(west.r == 0 && west.g == 128 && west.b == 128);
    TEST_CHECK(terrain::unreal::colorsVertices(ViewMode::Rock));
    TEST_CHECK(!terrain::unreal::colorsVertices(ViewMode::Wireframe));
    TEST_CHECK(!terrain::unreal::colorsVertices(ViewMode::Dry));
}

// The vertices of a tile off the heightmap of its world.
std::size_t offCount(const terrain::Heightfield& field, const terrain::TileData& tile,
                     std::size_t& vertices) {
    const UnrealMesh mesh = terrain::unreal::convertMesh(tile.mesh, tile.originY);
    const int lod = tile.request.key.lod;
    const auto lattice =
        terrain::unreal::tileLatticeHeights(field, lod, tile.originX, tile.originZ);
    const auto off = terrain::unreal::offHeightmap(mesh, lod, {tile.originX, tile.originY,
                                                               tile.originZ}, lattice);
    TEST_CHECK(off.size() == mesh.positions.size());
    vertices += off.size();
    std::size_t count = 0;
    for (const std::uint8_t o : off) {
        count += o;
    }
    // The colors of the mode: orange where off, gray elsewhere; the other modes keep the colors.
    const auto rock = terrain::unreal::debugColors(ViewMode::Rock, mesh, lod, off);
    const auto lodColors = terrain::unreal::debugColors(ViewMode::Lod, mesh, lod, off);
    const auto normal = terrain::unreal::debugColors(ViewMode::Normal, mesh, lod, off);
    for (std::size_t v = 0; v < off.size(); ++v) {
        TEST_CHECK(sameColor(rock[v], off[v] != 0 ? VertexColor{255, 120, 0, 255}
                                                  : VertexColor{150, 150, 150, 255}));
        TEST_CHECK(sameColor(lodColors[v], terrain::unreal::lodColor(lod)));
        TEST_CHECK(sameColor(normal[v], mesh.colors[v]));
    }
    return count;
}

void testHeightmap(const water_level::World& world) {
    std::size_t vertices = 0;
    std::size_t off = 0;
    const terrain::ChunkCoord mountain = streaming_test::mountainChunk(world.field());
    const double mx = mountain.x * terrain::kChunkSizeMeters;
    const double mz = mountain.z * terrain::kChunkSizeMeters;
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        for (const auto& [x, z] : {std::pair{0.0, 0.0}, std::pair{3000.0, -5000.0},
                                   std::pair{mx, mz}}) {
            off += offCount(world.field(), water_level::tileAt(world.tiles, x, z, lod), vertices);
        }
    }
    std::printf("  natural relief: %zu of %zu vertices off the heightmap\n", off, vertices);
    TEST_CHECK(vertices > 1000 && off == 0);
}

void testCavesAndEdits(const water_level::World& world) {
    std::size_t vertices = 0;
    std::size_t caves = 0;
    for (int i = 0; i < 40 && caves == 0; ++i) {
        const double x = 512.0 * i;
        caves += offCount(world.field(), water_level::tileAt(world.caveTiles, x, -x, 0),
                          vertices);
    }
    std::printf("  caves: %zu vertices off the heightmap\n", caves);
    TEST_CHECK(caves > 0);
    // An explosion in the ground: its crater is off the heightmap.
    terrain::TerrainTiles tiles{water_level::kSeed, terrain::HeightfieldParams{},
                                terrain::EcologyParams{}, std::nullopt, golden::kSmallPlanet,
                                world.hydrology};
    const double x = 3000.0 + 72.0;
    const double z = -5000.0 + 40.0;
    terrain::VolumeEdit blast;
    blast.shape = terrain::EditShape::Explosion;
    blast.mode = terrain::EditMode::Remove;
    blast.radius = 6 * 16; // sixteenths of a meter: 6 m
    blast.anchor = {static_cast<int>(x), static_cast<int>(tiles.heightfield().heightAt(x, z)),
                    static_cast<int>(z)};
    (void)tiles.applyEdit(blast);
    const std::size_t crater =
        offCount(tiles.heightfield(), water_level::tileAt(tiles, x, z, 0), vertices);
    std::printf("  edit: %zu vertices off the heightmap\n", crater);
    TEST_CHECK(crater > 10);
}

} // namespace

int main() {
    testColors();
    const water_level::World world;
    testHeightmap(world);
    testCavesAndEdits(world);
    return test::result();
}

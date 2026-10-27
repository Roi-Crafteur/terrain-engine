// Altitude color of the Unreal plugin: the colors of the bands, linear in between, continuous,
// clamped beyond the first and last bands, opaque; the sea floor only under the sea; and one color
// per vertex of a converted tile, from its world altitude and the ocean of its surface blend, the
// same for the same tile.
#include "altitude_color.hpp"
#include "tile_blend_atlas.hpp"
#include "tile_mesh_conversion.hpp"

#include "terrain/streaming/terrain_tiles.hpp"

#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

using terrain::unreal::VertexColor;
using terrain::unreal::altitudeColor;
using terrain::unreal::groundColor;
using terrain::unreal::TileSea;

// Within one byte of round(255 * (r, g, b)), opaque.
bool closeTo(VertexColor color, double r, double g, double b) {
    const auto close = [](std::uint8_t byte, double value) {
        return std::abs(static_cast<double>(byte) - value * 255.0) <= 1.0;
    };
    return close(color.r, r) && close(color.g, g) && close(color.b, b) && color.a == 255;
}

int channelGap(VertexColor a, VertexColor b) {
    const auto gap = [](std::uint8_t x, std::uint8_t y) { return std::abs(int{x} - int{y}); };
    return std::max({gap(a.r, b.r), gap(a.g, b.g), gap(a.b, b.b), gap(a.a, b.a)});
}

void testBands() {
    TEST_CHECK(closeTo(altitudeColor(terrain::unreal::kSeabedDepth), 0.20, 0.25, 0.32));
    TEST_CHECK(closeTo(altitudeColor(0.0), 0.76, 0.70, 0.50)); // sand at sea level
    TEST_CHECK(closeTo(altitudeColor(terrain::unreal::kBeachTop), 0.30, 0.55, 0.25));
    TEST_CHECK(closeTo(altitudeColor(terrain::unreal::kRockAltitude), 0.65, 0.60, 0.55));
    TEST_CHECK(closeTo(altitudeColor(2800.0), 0.65, 0.60, 0.55));
    TEST_CHECK(closeTo(altitudeColor(terrain::unreal::kSnowFull), 0.95, 0.95, 0.97));
    // Halfway between the green of the beach top and the gray of the rock.
    const double middle = (terrain::unreal::kBeachTop + terrain::unreal::kRockAltitude) / 2.0;
    TEST_CHECK(closeTo(altitudeColor(middle), 0.475, 0.575, 0.40));
}

void testClamped() {
    TEST_CHECK(altitudeColor(-11000.0) == altitudeColor(terrain::unreal::kSeabedDepth));
    TEST_CHECK(altitudeColor(9000.0) == altitudeColor(terrain::unreal::kSnowFull));
    TEST_CHECK(altitudeColor(std::numeric_limits<double>::quiet_NaN()) ==
               altitudeColor(terrain::unreal::kSeabedDepth));
}

// Every 10 cm from below the sea floor to above the snow, no channel jumps more than a byte.
void testContinuous() {
    int worst = 0;
    VertexColor previous = altitudeColor(-600.0);
    for (int step = 1; step <= 42000; ++step) {
        const VertexColor color = altitudeColor(-600.0 + step * 0.1);
        worst = std::max(worst, channelGap(previous, color));
        previous = color;
    }
    TEST_CHECK(worst <= 1);
}

// The sea floor under the sea only: dry ground below the sea level takes the color of the sea
// level, and nothing changes above it.
void testGroundColor() {
    TEST_CHECK(groundColor(-50.0, true) == altitudeColor(-50.0));
    TEST_CHECK(groundColor(-50.0, false) == altitudeColor(0.0));
    TEST_CHECK(groundColor(-50.0, false) != altitudeColor(-50.0));
    TEST_CHECK(groundColor(120.0, false) == altitudeColor(120.0));
    TEST_CHECK(groundColor(120.0, true) == altitudeColor(120.0));
}

terrain::BlendTexel pure(terrain::Biome biome) {
    terrain::SurfaceBlend blend;
    blend.biomes = {biome, terrain::Biome::Desert, terrain::Biome::Grassland};
    blend.biomeWeights = {1.0f, 0.0f, 0.0f};
    blend.materialWeights = {1.0f, 0.0f, 0.0f};
    return terrain::packBlend(blend);
}

// The ocean of a tile, bilinear between its texels: the ocean on the column x = 0, the desert
// elsewhere.
void testOceanShare() {
    constexpr int kSide = terrain::kTileBlendTexels;
    std::vector<terrain::BlendTexel> texels(kSide * kSide, pure(terrain::Biome::Desert));
    for (int k = 0; k < kSide; ++k) {
        texels[static_cast<std::size_t>(k * kSide)] = pure(terrain::Biome::Ocean);
    }
    const TileSea sea{&texels, 4.0};
    TEST_CHECK(terrain::unreal::oceanShare(sea, 0.0, 5.0) == 1.0);
    TEST_CHECK(std::abs(terrain::unreal::oceanShare(sea, 2.0, 7.0) - 0.5) < 1e-6);
    TEST_CHECK(terrain::unreal::oceanShare(sea, 4.0, 0.0) == 0.0);
    TEST_CHECK(terrain::unreal::oceanShare(sea, -3.0, 100.0) == 1.0); // clamped to the tile
    bool refused = false;
    try {
        const std::vector<terrain::BlendTexel> few(3);
        static_cast<void>(terrain::unreal::oceanShare({&few, 4.0}, 0.0, 0.0));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    TEST_CHECK(refused);
    // A mesh below the sea level: the sea floor on the ocean column, the dry color away from it.
    terrain::Mesh mesh;
    mesh.vertices = {{0.0f, 0.0f, 0.0f}, {16.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 16.0f}};
    mesh.normals.assign(3, {0.0f, 1.0f, 0.0f});
    mesh.indices = {0, 1, 2};
    const terrain::unreal::UnrealMesh out = terrain::unreal::convertMesh(mesh, -40.0, sea);
    TEST_CHECK(out.colors[0] == altitudeColor(-40.0) && out.colors[2] == altitudeColor(-40.0));
    TEST_CHECK(out.colors[1] == altitudeColor(0.0));
    TEST_CHECK(terrain::unreal::convertMesh(mesh, -40.0).colors[1] == altitudeColor(-40.0));
}

// Each vertex of a converted tile gets the color of its world altitude.
void testTileColors() {
    const terrain::TerrainTiles tiles(streaming_test::kSeed);
    const terrain::ChunkCoord m = streaming_test::mountainChunk(tiles.heightfield());
    const terrain::TileData data = tiles.generate({{m, 1}, 0});
    const terrain::unreal::UnrealMesh out =
        terrain::unreal::convertMesh(data.mesh, data.originY);
    TEST_CHECK(!out.empty());
    TEST_CHECK(out.colors.size() == data.mesh.vertices.size());
    bool byAltitude = true;
    for (std::size_t i = 0; i < out.colors.size(); ++i) {
        const double altitude = data.originY + data.mesh.vertices[i].y;
        byAltitude = byAltitude && out.colors[i] == altitudeColor(altitude);
    }
    TEST_CHECK(byAltitude);
    const terrain::TerrainTiles again(streaming_test::kSeed);
    const terrain::TileData same = again.generate({{m, 1}, 0});
    TEST_CHECK(terrain::unreal::convertMesh(same.mesh, same.originY).colors == out.colors);
    // With the sea of the tile, in the mountains: no ocean, the same colors above the sea level.
    const TileSea sea{&data.surfaceBlend, terrain::unreal::blendPitchMeters(data.request.key.lod)};
    const terrain::unreal::UnrealMesh flagged =
        terrain::unreal::convertMesh(data.mesh, data.originY, sea);
    bool dry = true;
    for (std::size_t i = 0; i < flagged.colors.size(); ++i) {
        const double altitude = data.originY + data.mesh.vertices[i].y;
        dry = dry && flagged.colors[i] == groundColor(altitude, false);
    }
    TEST_CHECK(dry);
}

} // namespace

int main() {
    testBands();
    testClamped();
    testContinuous();
    testGroundColor();
    testOceanShare();
    testTileColors();
    return test::result();
}

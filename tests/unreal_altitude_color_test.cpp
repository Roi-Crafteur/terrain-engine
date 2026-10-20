// Altitude color of the Unreal plugin: the colors of the bands, linear in between, continuous,
// clamped beyond the first and last bands, opaque; and one color per vertex of a converted tile,
// from its world altitude, the same for the same tile.
#include "altitude_color.hpp"
#include "tile_mesh_conversion.hpp"

#include "terrain/streaming/terrain_tiles.hpp"

#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace {

using terrain::unreal::VertexColor;
using terrain::unreal::altitudeColor;

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
}

} // namespace

int main() {
    testBands();
    testClamped();
    testContinuous();
    testTileColors();
    return test::result();
}

// The edit weights of the vertices and the light floor they feed in Unreal (agent 41):
// - a world without edits, or with edits out of reach of a tile, gives the tiles no weights (the
//   tiles stay bit for bit those of a world without edits);
// - a hole of 3 x 3 x 2 blocks: one weight per vertex at every LOD that reads it, 1 on the floor
//   of the hole at LOD 0, above 0 on its relay at LOD 1 and 2, 0 on the ground 8 m and two cells
//   away and more, in [0, 1] everywhere;
// - convertMesh puts the weights in the blue of the colors (the red, green and alpha kept) and
//   marks the mesh edited; without weights the colors are those of the altitude and the mesh is
//   not marked; weights of another count are refused;
// - the streamer hands Unreal the weights of a tile with its mesh.
#include "tile_mesh_conversion.hpp"
#include "tile_streamer.hpp"

#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include "terrain/streaming/terrain_tiles.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {

using terrain::TerrainTiles;
using terrain::TileData;
using terrain::TileRequest;
using terrain::unreal::UnrealMesh;

constexpr int kX = 44;
constexpr int kZ = 44;

TileData tileOver(const TerrainTiles& tiles, int lod) {
    return tiles.generate({terrain::tileContaining({kX / 16, kZ / 16}, lod), 0});
}

void dig(TerrainTiles& tiles, int dx) {
    for (int z = kZ - 1; z <= kZ + 1; ++z) {
        for (int x = kX - 1; x <= kX + 1; ++x) {
            const int top = static_cast<int>(std::floor(tiles.heightfield().heightAt(x + dx, z)));
            tiles.setBlock({x + dx, top, z}, terrain::BlockEdit::Removed);
            tiles.setBlock({x + dx, top - 1, z}, terrain::BlockEdit::Removed);
        }
    }
}

void testWithoutEdits() {
    TerrainTiles plain(terrain::kDefaultWorldSeed);
    TerrainTiles far(terrain::kDefaultWorldSeed);
    dig(far, 10'000);
    for (int lod = 0; lod <= terrain::kMaxLod; ++lod) {
        const TileData a = tileOver(plain, lod);
        const TileData b = tileOver(far, lod);
        TEST_CHECK(a.editWeights.empty() && a == b);
    }
}

void testHole() {
    TerrainTiles tiles(terrain::kDefaultWorldSeed);
    dig(tiles, 0);
    const double floor = std::floor(tiles.heightfield().heightAt(kX, kZ)) - 1.5;
    for (int lod = 0; lod <= 2; ++lod) {
        const TileData data = tileOver(tiles, lod);
        TEST_CHECK(data.editWeights.size() == data.mesh.vertices.size());
        float bottom = 0.0f;
        float far = 0.0f;
        bool bounded = true;
        for (std::size_t n = 0; n < data.mesh.vertices.size(); ++n) {
            const float w = data.editWeights[n];
            bounded = bounded && w >= 0.0f && w <= 1.0f;
            const double x = data.originX + data.mesh.vertices[n].x - kX;
            const double y = data.originY + data.mesh.vertices[n].y;
            const double z = data.originZ + data.mesh.vertices[n].z - kZ;
            const double across = std::max(std::abs(x), std::abs(z));
            // At LOD 0 the floor of the hole; beyond, its relay on the column of its cell.
            const bool onFloor = lod == 0 ? across <= 1.0 && y < floor + 1.0
                                          : across <= 1.5 + terrain::tileCellMeters(lod) / 2.0;
            if (onFloor) {
                bottom = std::max(bottom, w);
            }
            if (across >= 8.0 + 2.0 * terrain::tileCellMeters(lod)) {
                far = std::max(far, w);
            }
        }
        std::printf("a hole of 3 x 3 x 2 blocks, LOD %d: weight %.2f on it, at most %.2f far from "
                    "it\n",
                    lod, bottom, far);
        TEST_CHECK(bounded);
        TEST_CHECK(far == 0.0f);
        TEST_CHECK(lod > 0 || bottom >= 0.99f);
        TEST_CHECK(lod == 0 || bottom > 0.0f);
    }
}

void testConversion() {
    TerrainTiles tiles(terrain::kDefaultWorldSeed);
    dig(tiles, 0);
    const TileData data = tileOver(tiles, 0);
    const UnrealMesh plain = terrain::unreal::convertMesh(data.mesh, data.originY);
    const UnrealMesh edited =
        terrain::unreal::convertMesh(data.mesh, data.originY, {}, {}, {}, {}, data.editWeights);
    TEST_CHECK(!plain.edited && edited.edited);
    bool kept = plain.colors.size() == edited.colors.size();
    bool blue = kept;
    for (std::size_t n = 0; kept && n < plain.colors.size(); ++n) {
        const auto& a = plain.colors[n];
        const auto& b = edited.colors[n];
        kept = a.r == b.r && a.g == b.g && a.a == b.a;
        blue = blue && b.b == static_cast<std::uint8_t>(std::lround(255.0 * data.editWeights[n]));
    }
    TEST_CHECK(kept && blue);
    const std::vector<float> fewer(data.editWeights.begin(), data.editWeights.end() - 1);
    bool refused = false;
    try {
        (void)terrain::unreal::convertMesh(data.mesh, data.originY, {}, {}, {}, {}, fewer);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    TEST_CHECK(refused);
}

void testStreamer() {
    const terrain::TileKey marked = terrain::tileContaining({0, 0}, 0);
    terrain::StreamParams params;
    params.lod = streaming_test::smallLod();
    params.workerThreads = 0;
    terrain::unreal::TileStreamer streamer(
        [marked](const TileRequest& request) {
            TileData data;
            data.request = request;
            data.mesh.vertices = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}};
            data.mesh.normals.assign(3, {0.0f, 1.0f, 0.0f});
            data.mesh.indices = {0, 1, 2};
            if (request.key == marked) {
                data.editWeights = {0.0f, 0.5f, 1.0f};
            }
            return data;
        },
        params);
    const terrain::unreal::UnrealFrame frame{{0.0, 0.0, 0.0}};
    const terrain::unreal::UnrealPoint player = terrain::unreal::toUnreal(frame, {8.0, 0.0, 8.0});
    bool found = false;
    bool others = true;
    for (int n = 0; n < 50; ++n) {
        streamer.generatePending(64);
        for (const terrain::unreal::TileUpload& upload : streamer.update(player, frame).uploads) {
            if (upload.key == marked) {
                found = upload.mesh.edited && upload.mesh.colors[0].b == 0 &&
                        upload.mesh.colors[1].b == 128 && upload.mesh.colors[2].b == 255;
            } else {
                others = others && !upload.mesh.edited;
            }
        }
    }
    TEST_CHECK(found && others);
}

} // namespace

int main() {
    testWithoutEdits();
    testHole();
    testConversion();
    testStreamer();
    return test::result();
}

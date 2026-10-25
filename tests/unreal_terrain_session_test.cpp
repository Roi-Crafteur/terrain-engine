// Session of the Unreal plugin on the real world: the frame on the ground of a point, the ground
// under an Unreal position, the warm-up under a player with its collision tile and its surface
// blend, the timings of the generations and conversions, invalid parameters, and the destruction
// of a session while its workers generate tiles.
#include "terrain_session.hpp"

#include "test_assert.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>

namespace {

using terrain::ChunkCoord;
using terrain::TileKey;
using terrain::unreal::SessionParams;
using terrain::unreal::TerrainSession;
using terrain::unreal::TileStage;
using terrain::unreal::TileUpload;
using terrain::unreal::UnrealFrame;
using terrain::unreal::UnrealPoint;

SessionParams smallSession() {
    SessionParams params;
    params.stream.lod = {{2, 4, 8}, 6};
    params.stream.workerThreads = 2;
    params.stream.maxLoadsPerUpdate = 16;
    return params;
}

UnrealPoint centerOf(const UnrealFrame& frame, ChunkCoord chunk) {
    return terrain::unreal::toUnreal(frame, {(chunk.x + 0.5) * terrain::kChunkSizeMeters, 0.0,
                                             (chunk.z + 0.5) * terrain::kChunkSizeMeters});
}

std::optional<TileUpload> uploadOf(const terrain::unreal::WarmUpResult& warm, const TileKey& key) {
    std::optional<TileUpload> found;
    for (const terrain::unreal::StreamerUpdate& update : warm.updates) {
        for (const TileUpload& upload : update.uploads) {
            if (upload.key == key) {
                found = upload;
            }
        }
    }
    return found;
}

// The frame puts the ground of a point at the Unreal origin; groundZ follows the heightfield.
void testFrameAndGround() {
    const TerrainSession session(smallSession());
    const terrain::Heightfield& field = session.tiles().heightfield();
    const UnrealFrame frame = session.frameAt(1027.75, -33803.5);
    TEST_CHECK(frame.origin.x == 1027.75 && frame.origin.z == -33803.5);
    TEST_CHECK(frame.origin.y == field.heightAt(1027.75, -33803.5));
    TEST_CHECK(session.groundZ(frame, {0.0, 0.0, 12345.0}) == 0.0);
    const UnrealPoint point{1600.0, -800.0, 0.0}; // 16 m east, 8 m north of the origin
    const double expected =
        (field.heightAt(1027.75 + 16.0, -33803.5 - 8.0) - frame.origin.y) * 100.0;
    TEST_CHECK(std::abs(session.groundZ(frame, point) - expected) < 1e-6);
}

// The warm-up shows the ground under the player, with collision, and the timings count it.
void testWarmUp() {
    TerrainSession session(smallSession());
    const UnrealFrame frame = session.frameAt(0.0, 0.0);
    const ChunkCoord player{0, 0};
    const terrain::unreal::WarmUpResult warm = session.streamer().warmUp(
        centerOf(frame, player), frame, 1, std::chrono::seconds(60));
    TEST_CHECK(warm.complete);
    TEST_CHECK(session.streamer().shown(TileKey{player, 0}));
    const std::optional<TileUpload> ground = uploadOf(warm, TileKey{player, 0});
    TEST_CHECK(ground.has_value());
    if (ground) {
        TEST_CHECK(ground->collision);
        TEST_CHECK(!ground->mesh.empty());
        const terrain::TileData* data = session.streamer().manager().find(TileKey{player, 0});
        TEST_CHECK(data != nullptr &&
                   ground->origin == terrain::unreal::tileOrigin(frame, *data, TileKey{player, 0}));
        // The surface blend of the tile goes to the materials as the core made it.
        TEST_CHECK(data != nullptr && ground->blend == data->surfaceBlend);
        TEST_CHECK(ground->blend.size() ==
                   static_cast<std::size_t>(terrain::kTileBlendTexels * terrain::kTileBlendTexels));
    }
    const terrain::unreal::TimingsReport report = session.timings().snapshot();
    TEST_CHECK(report[0].stage(TileStage::Generation).tiles >= 9);
    TEST_CHECK(report[0].stage(TileStage::Conversion).tiles >= 9);
    TEST_CHECK(report[0].meshes == report[0].stage(TileStage::Conversion).tiles);
    // Two chunks east: the warm-up there completes too, LOD 0 tiles with collision only.
    const terrain::unreal::WarmUpResult moved = session.streamer().warmUp(
        centerOf(frame, {2, 0}), frame, 1, std::chrono::seconds(60));
    TEST_CHECK(moved.complete);
    bool collisionOnLod0 = true;
    for (const terrain::unreal::StreamerUpdate& update : moved.updates) {
        for (const TileUpload& upload : update.uploads) {
            collisionOnLod0 = collisionOnLod0 && upload.collision == (upload.key.lod == 0);
        }
    }
    TEST_CHECK(collisionOnLod0);
}

void testInvalidParams() {
    SessionParams params = smallSession();
    params.stream.maxLoadsPerUpdate = 0;
    bool rejected = false;
    try {
        const TerrainSession session(params);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    TEST_CHECK(rejected);
}

// A session destroyed while its workers generate the queued tiles (the default rings, with
// caves and their regions) stops once their current tiles are done.
void testDestroyWhileGenerating() {
    for (const bool caves : {false, true}) {
        SessionParams params;
        params.caves = caves;
        params.caveRegions = caves;
        params.stream.workerThreads = 3;
        const auto start = std::chrono::steady_clock::now();
        {
            TerrainSession session(params);
            const UnrealFrame frame = session.frameAt(0.0, 0.0);
            static_cast<void>(session.streamer().update(centerOf(frame, {0, 0}), frame));
            TEST_CHECK(session.streamer().manager().pendingCount() > 100);
        }
        TEST_CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(60));
    }
}

} // namespace

int main() {
    testFrameAndGround();
    testWarmUp();
    testInvalidParams();
    testDestroyWhileGenerating();
    return test::result();
}

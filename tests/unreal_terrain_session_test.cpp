// Session of the Unreal plugin on the real world: the frame on the ground of a point, the ground
// under an Unreal position, the warm-up under a player with its collision tile, the timings of
// the generations and conversions, invalid parameters, and the destruction of a session while
// its workers generate tiles; with a planet file (in a folder whose name is not ASCII), the water
// of the world in its tiles and its level at their vertices.
#include "terrain_session.hpp"

#include "storage_test_support.hpp"
#include "test_assert.hpp"

#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

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

// The water of the tiles uploaded around a point under a river: at each vertex on a column of
// the core, the Unreal Z of the vertex plus its depth is the level of the water of the core
// there (the vertex water, UV channel 0); returns the vertices checked, -1 on a mismatch.
long long checkUploadedWater(TerrainSession& session, double x, double z) {
    const UnrealFrame frame = session.frameAt(x, z);
    const terrain::unreal::WarmUpResult warm =
        session.streamer().warmUp({0.0, 0.0, 0.0}, frame, 1, std::chrono::seconds(60));
    long long checked = 0;
    for (const terrain::unreal::StreamerUpdate& update : warm.updates) {
        for (const TileUpload& upload : update.uploads) {
            const terrain::TileData* data = session.streamer().manager().find(upload.key);
            if (data == nullptr || upload.key.lod != 0 ||
                upload.mesh.water.size() != data->mesh.vertices.size()) {
                continue;
            }
            for (std::size_t v = 0; v < data->mesh.vertices.size(); ++v) {
                const terrain::Vec3f p = data->mesh.vertices[v];
                if (upload.mesh.water[v].wet == 0.0f || p.x != std::floor(p.x) ||
                    p.z != std::floor(p.z)) {
                    continue;
                }
                const double cx = data->originX + p.x;
                const double cz = data->originZ + p.z;
                const float level = session.tiles().heightfield().sampleAt(cx, cz).waterLevel;
                const double expected = terrain::unreal::toUnreal(frame, {cx, level, cz}).z;
                const double shown = upload.origin.z + upload.mesh.positions[v].z +
                                     100.0 * upload.mesh.water[v].depth;
                if (std::abs(shown - expected) > 0.1) {
                    return -1;
                }
                ++checked;
            }
        }
    }
    return checked;
}

// With a planet file (on a small world: the level of the whole world takes minutes), in a folder
// whose name is not ASCII (UTF-8, as ATerrainWorld::PlanetFile hands it over): the level built
// and written to it the first time, read from it the next; the heightfield of the tiles and of
// the horizon is the relief with its water, the valleys of its rivers carved (their floors
// filled by the levees, 5 m at most), and the tiles uploaded near a river carry its level.
void testPlanetFile() {
    const std::filesystem::path dir = storage_test::freshDirectory("terrain_session_planet");
    const std::filesystem::path folder =
        dir / std::filesystem::path(u8"Plan\u00e8te \u00e9t\u00e9"); // accented, in UTF-8
    std::filesystem::create_directories(folder);
    SessionParams params = smallSession();
    params.world = {32.0 * 65536.0, 16.0 * 65536.0};
    TEST_CHECK(TerrainSession(params).tiles().heightfield().hydrology() == nullptr);
    const std::u8string utf8 = (folder / "planet.bin").u8string();
    params.planetFile.assign(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    TerrainSession built(params);
    TEST_CHECK(std::filesystem::exists(folder / "planet.bin"));
    const terrain::Heightfield& field = built.tiles().heightfield();
    const terrain::Hydrology* hydrology = field.hydrology();
    TEST_CHECK(hydrology != nullptr && hydrology->reachCount() > 0);
    const TerrainSession read(params);
    const terrain::Hydrology* again = read.tiles().heightfield().hydrology();
    TEST_CHECK(again != nullptr &&
               again->planet().checksum() == hydrology->planet().checksum());
    if (hydrology != nullptr) {
        bool never = true;
        std::size_t carved = 0;
        for (std::size_t r = 0; r < hydrology->reachCount(); r += 97) {
            for (const terrain::RiverPoint& p : hydrology->reach(r).points) {
                // Carved, or filled by the levees 5 m at most.
                const float relief = hydrology->relief().heightAt(p.x, p.z);
                never = never && !(field.heightAt(p.x, p.z) > relief + 5.0f);
                carved += field.heightAt(p.x, p.z) < relief ? 1u : 0u;
            }
        }
        TEST_CHECK(never && carved > 0);
        long long checked = 0;
        for (std::size_t r = 0; r < hydrology->reachCount() && checked == 0; r += 97) {
            const terrain::RiverReach reach = hydrology->reach(r);
            const terrain::RiverPoint& p = reach.points[reach.points.size() / 2];
            if (field.sampleAt(p.x, p.z).water == terrain::WaterKind::River) {
                checked = checkUploadedWater(built, p.x, p.z);
            }
        }
        TEST_CHECK(checked > 0);
        std::printf("planet file: %lld wet vertices of the tiles at the level of their river\n",
                    checked);
    }
    std::filesystem::remove_all(dir);
}

} // namespace

int main() {
    testFrameAndGround();
    testWarmUp();
    testInvalidParams();
    testDestroyWhileGenerating();
    testPlanetFile();
    return test::result();
}

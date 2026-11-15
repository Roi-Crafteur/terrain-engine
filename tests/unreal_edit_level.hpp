#pragma once

// A level of the Unreal plugin played as ATerrainWorld::Tick plays it, for the tests of the edits
// through the streaming (agent 41: unreal_edit_streaming_test, unreal_edit_tiles_report_test): a
// session of the world of the planet, its streamer, the pacer of the uploads and the meshes as the
// backend keeps them.

#include "terrain_session.hpp"
#include "upload_pacer.hpp"

#include "test_assert.hpp"

#include "terrain/storage/volume_edit.hpp"

#include <chrono>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

namespace edit_level {

using terrain::TileKey;
using terrain::TileKeyHash;
using terrain::VolumeEdit;
using terrain::unreal::SessionParams;
using terrain::unreal::TerrainSession;
using terrain::unreal::TileMove;
using terrain::unreal::TileUpload;
using terrain::unreal::UnrealFrame;
using terrain::unreal::UnrealPoint;

// The meshes of Unreal as the backend keeps them (UProceduralMeshTerrainBackend): one per key,
// replaced in place, an empty one removed.
class Meshes final : public terrain::unreal::UploadSink {
public:
    void upload(const TileUpload& upload) override {
        if (upload.mesh.empty()) {
            tiles.erase(upload.key);
        } else {
            tiles.insert_or_assign(upload.key, upload);
        }
    }
    void move(const TileMove& move) override {
        const auto it = tiles.find(move.key);
        if (it != tiles.end()) {
            it->second.origin = move.origin;
        }
    }
    void remove(const TileKey& key) override { tiles.erase(key); }

    std::unordered_map<TileKey, TileUpload, TileKeyHash> tiles;
};

// The world of the planet with the rings of Terrain World; with a planet file (a local run), its
// water too.
inline SessionParams sessionParams(int workers, const std::string& planetFile) {
    SessionParams params;
    params.planetFile = planetFile;
    params.stream.lod = {{4, 8, 16}, 32};
    params.stream.workerThreads = workers;
    params.stream.maxLoadsPerUpdate = 16;
    return params;
}

// A level as Unreal plays it, frame by frame (ATerrainWorld::Tick): the edits taken
// (TickTerrainEdits), then a new update of the streamer only once the pacer applied the last one,
// a few uploads a frame (TickTerrainUploads), until nothing is left to load; the warm-up under a
// new pawn applies its updates at once (WarmUpAround, Terrain.Pawn).
struct Level {
    Level(int workers, double x, double z, const std::string& planetFile = {})
        : session(sessionParams(workers, planetFile)), frame(session.frameAt(x, z)) {}

    void play(double x, double z) {
        const UnrealPoint player = terrain::unreal::toUnreal(frame, {x, 0.0, z});
        terrain::unreal::UploadBudget budget;
        budget.milliseconds = 1e-9; // one upload a frame: the updates span many frames
        for (int frames = 0; frames < 1'000'000; ++frames) {
            static_cast<void>(session.takeEdits());
            bool idle = false;
            if (!pacer.busy()) {
                terrain::unreal::StreamerUpdate update = session.streamer().update(player, frame);
                idle = update.empty() && session.editsPending() == 0 &&
                       session.streamer().manager().pendingCount() == 0;
                pacer.start(std::move(update));
            }
            static_cast<void>(pacer.apply(meshes, budget));
            if (idle) {
                return;
            }
            if (session.streamer().generatePending(4) == 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
        }
        constexpr bool streamingBecameIdle = false;
        TEST_CHECK(streamingBecameIdle);
    }

    // Terrain.Pawn: the LOD 0 tiles around the pawn generated and shown at once.
    void warmUp(double x, double z) {
        static_cast<void>(pacer.flush(meshes));
        const terrain::unreal::WarmUpResult warm = session.streamer().warmUp(
            terrain::unreal::toUnreal(frame, {x, 0.0, z}), frame, 1, std::chrono::seconds(60));
        for (const terrain::unreal::StreamerUpdate& update : warm.updates) {
            pacer.start(update);
            static_cast<void>(pacer.flush(meshes));
        }
        play(x, z);
    }

    // A click: the edit queued on the edit thread, done, then the frames that show it.
    void click(const VolumeEdit& edit, double x, double z) {
        session.queueEdit(edit, "click");
        session.waitEdits();
        play(x, z);
    }

    TerrainSession session;
    UnrealFrame frame;
    terrain::unreal::UploadPacer pacer;
    Meshes meshes;
};

} // namespace edit_level

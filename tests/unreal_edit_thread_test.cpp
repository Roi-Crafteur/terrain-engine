// The edit thread of the session at work while the tiles and the far horizon are generated on
// other threads (Bridge/terrain_session, edit_thread): edits queued every few frames while the
// tile streamer (2 workers) follows a moving player and a GPU horizon runs its epochs and its
// detail normals on a pool of 3 threads. In the end the bits are those of the synchronous path:
// the masks of the tiles, the ground staged for the horizon, and the textures of the horizon
// (samples and detail normals) as a horizon started after the edits draws them. Built with
// -fsanitize=thread (cmake -DCMAKE_CXX_FLAGS=-fsanitize=thread), it also checks that no thread
// reaches the masks or the map of the edits without their lock or their turn.
#include "horizon_gpu_streamer.hpp"
#include "terrain_session.hpp"

#include "streaming_test_support.hpp"
#include "test_assert.hpp"
#include "unreal_edit_support.hpp"
#include "unreal_texture_mirror.hpp"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <set>
#include <thread>
#include <utility>
#include <vector>

namespace {

using terrain::unreal::HorizonGpuStreamer;
using terrain::unreal::TerrainSession;
using terrain::unreal::UnrealPoint;

constexpr double kX = streaming_test::kCenterX + 8.0;
constexpr double kZ = streaming_test::kCenterZ + 8.0;
constexpr double kStepCm = 200.0; // the player walks 2 m along x per frame

// Tiles of LOD 3 out to 23 chunks, as the horizon needs.
terrain::LodParams lod() {
    return {{1, 3, 7}, 23};
}

terrain::unreal::SessionParams sessionParams(int workers) {
    terrain::unreal::SessionParams params;
    params.stream.lod = lod();
    params.stream.workerThreads = workers;
    return params;
}

terrain::unreal::HorizonGpuParams gpuParams() {
    terrain::unreal::HorizonGpuParams params;
    params.levels = 5;
    params.cellsPerSide = 128;
    params.lod = lod();
    return params;
}

HorizonGpuStreamer horizonOf(TerrainSession& session, terrain::unreal::JobLauncher launch) {
    return {session.tiles().heightfield(),
            session.tiles().ecology(),
            session.tiles().world(),
            gpuParams(),
            std::move(launch),
            &session.horizonEdits()};
}

// Updates until the horizon has no job left and writes nothing more.
void settle(HorizonGpuStreamer& horizon, horizon_test::TextureMirror& textures,
            const UnrealPoint& player, const terrain::unreal::UnrealFrame& frame) {
    int quiet = 0;
    for (int n = 0; n < 400 && quiet < 3; ++n) {
        horizon.waitIdle();
        const terrain::unreal::HorizonGpuUpdate update = horizon.update(player, frame);
        textures.apply(update);
        quiet = update.empty() && !update.committed ? quiet + 1 : 0;
    }
}

void testEditsWhileStreaming() {
    TerrainSession sync(sessionParams(0));
    const std::vector<edit_test::Edit> edits =
        edit_test::someEdits(sync.tiles().heightfield(), kX, kZ);
    const std::vector<int> queuedAt = {0, 2, 3, 6, 9}; // frames

    edit_test::JobPool pool(3);
    TerrainSession session(sessionParams(2));
    const terrain::unreal::UnrealFrame frame = session.frameAt(kX, kZ);
    HorizonGpuStreamer horizon = horizonOf(session, pool.launcher());
    horizon_test::TextureMirror textures;
    std::vector<terrain::unreal::EditOutcome> outcomes;
    UnrealPoint player;
    std::size_t uploads = 0;
    int frames = 0;
    // Frames of about 10 ms, as a game: until the edits are done, 30 frames at least.
    for (int f = 0; f < 3000 && (f < 30 || outcomes.size() < edits.size()); ++f) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        for (std::size_t n = 0; n < edits.size(); ++n) {
            if (queuedAt[n] == f) {
                edit_test::queue(session, edits[n]);
            }
        }
        player = {kStepCm * f, 0.0, 0.0};
        frames = f + 1;
        for (terrain::unreal::EditOutcome& outcome : session.takeEdits()) {
            outcomes.push_back(std::move(outcome));
        }
        uploads += session.streamer().update(player, frame).uploads.size();
        textures.apply(horizon.update(player, frame));
    }
    settle(horizon, textures, player, frame);
    std::printf("%zu edits done in %d frames while %zu tiles were streamed and the player walked "
                "%.0f m\n",
                outcomes.size(), frames, uploads, player.x / 100.0);

    // The synchronous path: the same edits, then a horizon started after them.
    bool same = outcomes.size() == edits.size();
    std::set<terrain::EditColumn> columns;
    for (std::size_t n = 0; same && n < edits.size(); ++n) {
        const edit_test::SyncEdit expected = edit_test::applySync(sync, edits[n]);
        same = outcomes[n].label == edits[n].label &&
               (outcomes[n].error != nullptr) == expected.refused &&
               outcomes[n].chunks == expected.chunks &&
               edit_test::sameColumns(outcomes[n].columns, expected.columns);
        for (const terrain::EditedColumn& column : outcomes[n].columns) {
            columns.insert(column.column);
        }
    }
    TEST_CHECK(same && uploads > 0);
    TEST_CHECK(session.tiles().modifications() == sync.tiles().modifications());
    sync.horizonEdits().applyStaged();
    bool staged = !session.horizonEdits().hasStaged() &&
                  session.horizonEdits().columnCount() == sync.horizonEdits().columnCount();
    for (const terrain::EditColumn& column : columns) {
        staged = staged && session.horizonEdits().columnGround(column) ==
                               sync.horizonEdits().columnGround(column);
    }
    TEST_CHECK(staged);
    HorizonGpuStreamer fresh = horizonOf(sync, [](std::function<void()> job) { job(); });
    horizon_test::TextureMirror expected;
    settle(fresh, expected, player, frame);
    TEST_CHECK(textures.texels == expected.texels);
    TEST_CHECK(textures.normals == expected.normals);
}

} // namespace

int main() {
    testEditsWhileStreaming();
    return test::result();
}

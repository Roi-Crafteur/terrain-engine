// What a razed mountain costs the game thread on the GPU path (Bridge/terrain_session and its edit
// thread, horizon_gpu_streamer), as Terrain.Edit raze 11300 1000 500 in the K2 scene: from the
// valley of the K2, a square of 1 km razed 11.3 km ahead, 500 m under its lowest ground (its
// summit near 8,100 m). Measured on the game thread: the command, then every frame until the
// horizon shows the edit (the edits taken, then the update of the horizon, whose epoch applies the
// edit and later commits it); and the time from the command to that commit. Goals: under 16 ms
// per frame; shown under 1 s after the command, of which the edit thread takes the most (427 ms
// on the machine of the first test in Unreal, three times as long on slower ones): checked here
// as under 0.5 s besides the edit thread.
#include "horizon_gpu_streamer.hpp"
#include "terrain_edits.hpp"
#include "terrain_session.hpp"

#include "test_assert.hpp"
#include "unreal_edit_support.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <numbers>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using terrain::unreal::GroundEdit;
using terrain::unreal::HorizonGpuStreamer;
using terrain::unreal::TerrainSession;

// The valley of the K2 (horizon_cost_test), and the heading of the camera (Unreal yaw).
constexpr double kK2X = 416'857.0;
constexpr double kK2Z = -11'398'966.0;
constexpr double kYawDegrees = -65.5;

double millisecondsSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// K = 128 for a short test, with fewer tiles: the box on level 5 (Unreal: K = 256, level 4). What
// the game thread does for the edit (taking it, applying it to the map of the edits, planning the
// epoch) does not depend on K; the samples the epoch writes, a few hundred, hardly.
terrain::unreal::HorizonGpuParams gpuParams() {
    terrain::unreal::HorizonGpuParams params;
    params.levels = 6;
    params.cellsPerSide = 128;
    params.lod = {{1, 3, 7}, 23}; // tiles within level 0
    return params;
}

void testRazedMountain() {
    terrain::unreal::SessionParams sessionParams;
    sessionParams.stream.workerThreads = 0; // the tiles are not streamed here
    TerrainSession session(sessionParams);
    const terrain::unreal::UnrealFrame frame = session.frameAt(kK2X, kK2Z);
    edit_test::JobPool pool(3);
    HorizonGpuStreamer horizon(session.tiles().heightfield(), session.tiles().ecology(),
                               session.tiles().world(), gpuParams(), pool.launcher(),
                               &session.horizonEdits());
    const terrain::unreal::UnrealPoint player{};
    const Clock::time_point started = Clock::now();
    for (int quiet = 0; quiet < 3;) {
        horizon.waitIdle();
        const terrain::unreal::HorizonGpuUpdate update = horizon.update(player, frame);
        quiet = update.empty() && !update.committed ? quiet + 1 : 0;
    }
    std::printf("horizon of 6 levels (K = 128) with its detail normals: %.1f s\n",
                millisecondsSince(started) / 1000.0);

    const double yaw = kYawDegrees * std::numbers::pi / 180.0;
    const double x = kK2X + std::cos(yaw) * 11'300.0;
    const double z = kK2Z + std::sin(yaw) * 11'300.0;
    const Clock::time_point command = Clock::now();
    session.queueGroundEdit({GroundEdit::Raze, 11'300.0, 1'000.0, 500.0}, x, z, "raze");
    const double queued = millisecondsSince(command);
    // Frames of 10 ms until an epoch carrying the edit commits.
    std::vector<terrain::unreal::EditOutcome> done;
    double slowest = 0.0;
    double shown = -1.0;
    int frames = 0;
    for (; frames < 1000 && shown < 0.0; ++frames) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const Clock::time_point start = Clock::now();
        for (terrain::unreal::EditOutcome& outcome : session.takeEdits()) {
            done.push_back(std::move(outcome));
        }
        const terrain::unreal::HorizonGpuUpdate update = horizon.update(player, frame);
        slowest = std::max(slowest, millisecondsSince(start));
        if (!done.empty() && update.committed && update.samples > 0) {
            shown = millisecondsSince(command);
        }
    }
    TEST_CHECK(done.size() == 1 && done[0].error == nullptr && shown > 0.0);
    if (done.size() != 1 || shown < 0.0) {
        return;
    }
    const terrain::unreal::EditOutcome& raze = done[0];
    std::printf("razed mountain: %zu chunks, %zu columns; command %.2f ms; edit thread %.0f ms "
                "(edit %.0f ms, ground for the horizon %.0f ms); staged %.2f ms\n",
                raze.chunks.size(), raze.columns.size(), queued,
                raze.editMilliseconds + raze.columnMilliseconds, raze.editMilliseconds,
                raze.columnMilliseconds, raze.stageMilliseconds);
    std::printf("game thread: at most %.2f ms per frame over %d frames; shown far away %.0f ms "
                "after the command\n",
                slowest, frames, shown);
    TEST_CHECK(queued < 1.0 && slowest < 16.0);
    TEST_CHECK(shown < raze.editMilliseconds + raze.columnMilliseconds + 500.0);
}

} // namespace

int main() {
    testRazedMountain();
    return test::result();
}

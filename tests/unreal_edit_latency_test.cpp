// The time from the click of a player to the tile it changed shown again (agent 28): the order of
// the steps, an edit refused, a tile of another LOD or of another place, the seam of the world,
// the summary; then measured on a real session, frame after frame as the game thread runs them
// (take the edits done, update the streamer, upload), for each tool (printed for the report).
#include "edit_latency.hpp"

#include "block_targeting.hpp"
#include "terrain_session.hpp"
#include "test_assert.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

using terrain::ChunkCoord;
using terrain::TileKey;
using terrain::WorldSize;
using terrain::unreal::EditLatency;

void testSteps() {
    EditLatency latency;
    const WorldSize world;
    const std::uint64_t a = latency.clicked(1.0, 100);
    const std::uint64_t refused = latency.clicked(1.1, 106);
    const std::vector<ChunkCoord> chunks = {ChunkCoord{3, -2}, ChunkCoord{4, -2}};
    // An upload before its outcome is taken shows nothing.
    TEST_CHECK(latency.uploaded(TileKey{ChunkCoord{2, -2}, 0}, world, 1.01, 101) == 0);
    latency.done(a, chunks, 1.004);
    latency.done(refused, {}, 1.2);
    TEST_CHECK(latency.waiting() == 1);
    // Another LOD, or a tile of another place: still waiting.
    TEST_CHECK(latency.uploaded(TileKey{ChunkCoord{0, -4}, 1}, world, 1.02, 102) == 0);
    TEST_CHECK(latency.uploaded(TileKey{ChunkCoord{6, -2}, 0}, world, 1.02, 102) == 0);
    // The LOD 0 tile of two chunks over (4, -2): shown.
    TEST_CHECK(latency.uploaded(TileKey{ChunkCoord{4, -2}, 0}, world, 1.016, 101) == 1);
    TEST_CHECK(latency.waiting() == 0);
    const terrain::unreal::LatencySummary summary = latency.summary();
    TEST_CHECK(summary.count == 1 && summary.framesMax == 1);
    TEST_CHECK(std::abs(summary.shownMedian - 16.0) < 1e-9);
    TEST_CHECK(std::abs(summary.doneMedian - 4.0) < 1e-9);
}

void testSeamAndSummary() {
    EditLatency latency(4);
    const WorldSize world;
    const int half = static_cast<int>(world.x / terrain::kChunkSizeMeters) / 2;
    // A chunk past the east edge is the chunk at the west edge: the tile there shows it.
    const std::uint64_t id = latency.clicked(0.0, 0);
    const std::vector<ChunkCoord> east = {ChunkCoord{half, 0}};
    latency.done(id, east, 0.001);
    TEST_CHECK(latency.uploaded(TileKey{ChunkCoord{-half, 0}, 0}, world, 0.002, 0) == 1);
    for (int i = 1; i <= 6; ++i) {
        const std::uint64_t next = latency.clicked(i, i * 10);
        latency.done(next, east, i + 0.001);
        TEST_CHECK(latency.uploaded(TileKey{ChunkCoord{-half, 0}, 0}, world, i + 0.001 * i,
                                    i * 10 + i) == 1);
    }
    const terrain::unreal::LatencySummary summary = latency.summary();
    TEST_CHECK(summary.count == 4); // the last 4 kept
    TEST_CHECK(std::abs(summary.shownMax - 6.0) < 1e-6 && summary.framesMax == 6);
    TEST_CHECK(std::abs(summary.shownMedian - 4.0) < 1e-6);
}

// A real session at a few frames a second at most: the edits of each tool clicked at the ground
// and measured until their tile is uploaded. The meshes are not drawn: the time of Unreal to
// draw them is measured in Unreal (Terrain.EditStats).
void testSession() {
    terrain::unreal::SessionParams params;
    params.stream.lod = {{2, 4, 8}, 6};
    params.stream.workerThreads = 2;
    params.stream.maxLoadsPerUpdate = 16;
    terrain::unreal::TerrainSession session(params);
    const terrain::unreal::UnrealFrame frame = session.frameAt(8.0, 8.0);
    const terrain::unreal::UnrealPoint player = terrain::unreal::toUnreal(frame, {8.0, 0.0, 8.0});
    TEST_CHECK(session.streamer().warmUp(player, frame, 1, std::chrono::seconds(60)).complete);
    using Clock = std::chrono::steady_clock;
    const Clock::time_point start = Clock::now();
    const auto now = [&] { return std::chrono::duration<double>(Clock::now() - start).count(); };
    EditLatency latency;
    std::int64_t frameNumber = 0;
    const terrain::unreal::BuildTool tools[] = {terrain::unreal::BuildTool::Block,
                                                terrain::unreal::BuildTool::Sphere2,
                                                terrain::unreal::BuildTool::Sphere4};
    for (const terrain::unreal::BuildTool tool : tools) {
        for (int i = 0; i < 4; ++i) {
            const double x = 8.0 + i;
            const double y = session.tiles().heightfield().heightAt(x, 8.0);
            const terrain::unreal::BuildHit hit{{x, y, 8.0}, {0.0, 1.0, 0.0}, 3.0};
            const auto action = i % 2 == 0 ? terrain::unreal::BuildAction::Break
                                           : terrain::unreal::BuildAction::Place;
            const std::uint64_t id = latency.clicked(now(), frameNumber);
            session.queueEdit(buildTarget(hit, tool, action).edit, "build");
            for (int f = 0; f < 600 && latency.waiting() > 0; ++f, ++frameNumber) {
                for (const auto& outcome : session.takeEdits()) {
                    latency.done(id, outcome.chunks, now());
                }
                for (const auto& upload : session.streamer().update(player, frame).uploads) {
                    latency.uploaded(upload.key, params.world, now(), frameNumber);
                }
                if (latency.waiting() > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            TEST_CHECK(latency.waiting() == 0);
        }
        const terrain::unreal::LatencySummary summary = latency.summary();
        std::printf("%s: click -> done %.2f ms, click -> tile uploaded median %.2f ms, max %.2f "
                    "ms, %lld frames at most (1 ms a frame)\n",
                    terrain::unreal::toolName(tool), summary.doneMedian, summary.shownMedian,
                    summary.shownMax, static_cast<long long>(summary.framesMax));
        latency = EditLatency{};
    }
}

} // namespace

int main() {
    testSteps();
    testSeamAndSummary();
    testSession();
    return test::result();
}

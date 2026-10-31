// The timeline of a load (agent 38, Bridge/load_timeline.hpp): its milestones on a selection made
// by hand, the curve per band of distance and the CSV, the records of several threads, nothing
// recorded outside a load, and a load of the streamer traced from the generator to the meshes.
#include "load_timeline.hpp"
#include "tile_streamer.hpp"

#include "horizon_test_support.hpp"
#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include "terrain/streaming/horizon_sampler.hpp"

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

using terrain::ChunkCoord;
using terrain::TileData;
using terrain::TileKey;
using terrain::TileRequest;
using terrain::unreal::LoadClock;
using terrain::unreal::LoadExpectations;
using terrain::unreal::LoadMilestone;
using terrain::unreal::LoadReason;
using terrain::unreal::LoadSummary;
using terrain::unreal::LoadTimeline;
using terrain::unreal::SelectedTile;

const LoadClock::time_point kStart = LoadClock::now();

LoadClock::time_point at(double seconds) {
    return kStart + std::chrono::duration_cast<LoadClock::duration>(
                        std::chrono::duration<double>(seconds));
}

bool near(double a, double b) {
    return a > b - 1e-6 && a < b + 1e-6;
}

double milestone(const LoadSummary& summary, LoadMilestone which) {
    return summary.milestones[static_cast<std::size_t>(which)];
}

// Five tiles at 0, 1, 3, 10 and 20 chunks: the ground, the 64 m ring, the 256 m ring, the rest.
std::vector<SelectedTile> handMadeSelection() {
    return {{{{0, 0}, 0}, 0, false},
            {{{1, 0}, 0}, 1, false},
            {{{3, 0}, 0}, 3, false},
            {{{8, 0}, 2}, 10, false},
            {{{16, 0}, 3}, 20, false}};
}

void show(LoadTimeline& timeline, std::vector<SelectedTile>& tiles, std::size_t index,
          double seconds) {
    tiles[index].shown = true;
    timeline.recordShown(tiles[index].key, at(seconds), 1.5, tiles[index].key.lod == 0);
}

// Each tile milestone is reached when every tile within its reach is shown, once; the
// milestones of the caller are marked once; the load ends with the last one expected.
void testMilestonesByHand() {
    LoadTimeline timeline;
    timeline.begin(LoadReason::Teleport, 12.0, -34.0, kStart);
    std::vector<SelectedTile> tiles = handMadeSelection();
    TEST_CHECK(timeline.observe(tiles, at(0.05)).empty());
    show(timeline, tiles, 0, 0.08);
    show(timeline, tiles, 2, 0.09); // the 64 m ring waits for tile 1
    TEST_CHECK(timeline.observe(tiles, at(0.09)).empty());
    show(timeline, tiles, 1, 0.1);
    const std::vector<LoadMilestone> first = timeline.observe(tiles, at(0.1));
    TEST_CHECK(first == std::vector<LoadMilestone>{LoadMilestone::Ground, LoadMilestone::Ring64});
    TEST_CHECK(timeline.reached(LoadMilestone::Ground) &&
               !timeline.reached(LoadMilestone::GroundCollision));
    TEST_CHECK(timeline.mark(LoadMilestone::GroundCollision, at(0.15)));
    TEST_CHECK(!timeline.mark(LoadMilestone::GroundCollision, at(0.2)));
    show(timeline, tiles, 4, 0.3); // the far tile before the 256 m ring: only all tiles waits
    TEST_CHECK(timeline.observe(tiles, at(0.3)).empty());
    show(timeline, tiles, 3, 0.5);
    TEST_CHECK((timeline.observe(tiles, at(0.5)) ==
                std::vector<LoadMilestone>{LoadMilestone::Ring256, LoadMilestone::AllTiles}));
    TEST_CHECK(timeline.active() && !timeline.complete());
    TEST_CHECK(timeline.missing() ==
               (std::vector<LoadMilestone>{LoadMilestone::Horizon, LoadMilestone::HorizonDetail}));
    TEST_CHECK(timeline.mark(LoadMilestone::Horizon, at(1.0)));
    TEST_CHECK(timeline.mark(LoadMilestone::HorizonDetail, at(2.5)));
    TEST_CHECK(timeline.complete() && !timeline.active() && timeline.missing().empty());

    const LoadSummary summary = timeline.summary();
    TEST_CHECK(summary.reason == LoadReason::Teleport && summary.playerX == 12.0);
    TEST_CHECK(near(milestone(summary, LoadMilestone::Ground), 0.1));
    TEST_CHECK(near(milestone(summary, LoadMilestone::GroundCollision), 0.15));
    TEST_CHECK(near(milestone(summary, LoadMilestone::Ring64), 0.1));
    TEST_CHECK(near(milestone(summary, LoadMilestone::Ring256), 0.5));
    TEST_CHECK(near(milestone(summary, LoadMilestone::AllTiles), 0.5));
    TEST_CHECK(near(milestone(summary, LoadMilestone::Horizon), 1.0));
    TEST_CHECK(near(milestone(summary, LoadMilestone::HorizonDetail), 2.5));
    TEST_CHECK(summary.uploads == 5 && near(summary.uploadMilliseconds, 7.5));
}

// The curve per band: the instant the last tile of each band was shown, its tiles and their
// mean generation; the CSV, one row per tile, nearest first.
void testBandsAndCsv() {
    LoadTimeline timeline;
    timeline.begin(LoadReason::Spawn, 0.0, 0.0, kStart);
    std::vector<SelectedTile> tiles = handMadeSelection();
    timeline.recordGeneration(tiles[0].key, at(0.0), at(0.004), std::this_thread::get_id());
    timeline.recordGeneration(tiles[2].key, at(0.0), at(0.002), std::this_thread::get_id());
    timeline.recordGeneration(tiles[3].key, at(0.01), at(0.02), std::this_thread::get_id());
    for (std::size_t i = 0; i < 3; ++i) {
        show(timeline, tiles, i, 0.1 * static_cast<double>(i + 1));
    }
    timeline.observe(tiles, at(0.3));
    const LoadSummary summary = timeline.summary();
    TEST_CHECK(summary.bandTiles[0] == 1 && summary.bandTiles[1] == 1 &&
               summary.bandTiles[2] == 1 && summary.bandTiles[4] == 1 &&
               summary.bandTiles[5] == 1 && summary.bandTiles[3] == 0);
    TEST_CHECK(near(summary.bandShown[0], 0.1) && near(summary.bandShown[1], 0.2) &&
               near(summary.bandShown[2], 0.3));
    TEST_CHECK(summary.bandShown[3] < 0.0 && summary.bandShown[4] < 0.0); // empty, not shown
    TEST_CHECK(near(summary.bandGenerationMs[0], 4.0) && near(summary.bandGenerationMs[4], 10.0));
    TEST_CHECK(summary.threadTiles.size() == 1 && summary.threadTiles[0] == 3);
    TEST_CHECK(near(summary.threadBusySeconds[0], 0.016));

    const std::vector<std::string> rows =
        terrain::unreal::csvRows(timeline.traces());
    TEST_CHECK(rows.size() == tiles.size() + 1);
    TEST_CHECK(rows[0].rfind("chunk_x,chunk_z,lod,distance_m", 0) == 0);
    TEST_CHECK(rows[1] == "0,0,0,0,0,1,0.0000,0.0040,4.000,-1.0000,0.1000,1.500,1");
    TEST_CHECK(rows[4].rfind("8,0,2,160,0,1,", 0) == 0);
    const std::vector<std::string> lines = terrain::unreal::describe(summary);
    TEST_CHECK(lines.size() >= 3 && lines[1].find("ground 0.300 s") != std::string::npos);
    TEST_CHECK(terrain::unreal::describeMilestone(LoadMilestone::Ring64, -1.0) == "64 m not yet");
}

// Generations recorded from several threads at once: one index per thread, the tiles generated
// again counted.
void testRecordsFromThreads() {
    LoadTimeline timeline;
    timeline.begin(LoadReason::Spawn, 0.0, 0.0, kStart);
    constexpr int kThreads = 4;
    constexpr int kTilesPerThread = 50;
    {
        std::vector<std::jthread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&timeline, t] {
                for (int i = 0; i < kTilesPerThread; ++i) {
                    const TileKey key{{i, t}, 0};
                    timeline.recordGeneration(key, at(0.0), at(0.001), std::this_thread::get_id());
                }
            });
        }
    }
    timeline.recordGeneration({{0, 0}, 0}, at(0.1), at(0.2), std::this_thread::get_id());
    const LoadSummary summary = timeline.summary();
    TEST_CHECK(summary.tiles == static_cast<std::size_t>(kThreads * kTilesPerThread));
    TEST_CHECK(summary.threadTiles.size() == static_cast<std::size_t>(kThreads + 1));
    TEST_CHECK(summary.generatedAgain == 1);
}

// Nothing is recorded before the first load nor after the last milestone; a new load forgets
// the previous one.
void testOutsideALoad() {
    LoadTimeline timeline;
    timeline.recordShown({{0, 0}, 0}, at(0.1), 1.0, true);
    TEST_CHECK(!timeline.active() && timeline.summary().tiles == 0);
    const LoadExpectations tilesOnly{false, false, false};
    timeline.begin(LoadReason::Prepared, 0.0, 0.0, kStart, tilesOnly);
    std::vector<SelectedTile> tiles = handMadeSelection();
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        show(timeline, tiles, i, 0.1);
    }
    timeline.observe(tiles, at(0.2));
    TEST_CHECK(timeline.complete() && !timeline.active());
    timeline.recordShown({{40, 40}, 0}, at(0.3), 1.0, true);
    timeline.recordFrame(50.0, 3);
    TEST_CHECK(timeline.summary().tiles == tiles.size() && timeline.summary().frames == 0);
    TEST_CHECK(!timeline.mark(LoadMilestone::Horizon, at(0.4)));
    timeline.begin(LoadReason::Teleport, 0.0, 0.0, at(1.0));
    TEST_CHECK(timeline.active() && timeline.summary().tiles == 0);
    TEST_CHECK(near(timeline.secondsSince(at(1.25)), 0.25));
}

TileData fakeTile(const TileRequest& request) {
    TileData data;
    data.request = request;
    data.originX = request.key.origin.x * terrain::kChunkSizeMeters;
    data.originZ = request.key.origin.z * terrain::kChunkSizeMeters;
    data.mesh.vertices = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}};
    data.mesh.normals.assign(3, {0.0f, 1.0f, 0.0f});
    data.mesh.indices = {0, 1, 2};
    return data;
}

// A whole load of the streamer, traced from the generator (tracedGenerator) to the meshes: every
// selected tile generated once and shown, the bands shown nearest first.
void testStreamerLoad() {
    auto timeline = std::make_shared<LoadTimeline>();
    terrain::StreamParams params;
    params.lod = streaming_test::smallLod();
    params.workerThreads = 0;
    params.maxLoadsPerUpdate = 4;
    terrain::unreal::TileStreamer streamer(
        terrain::unreal::tracedGenerator(fakeTile, timeline), params);
    const terrain::unreal::UnrealFrame frame{};
    const ChunkCoord player{5, -3};
    const terrain::unreal::UnrealPoint position = terrain::unreal::toUnreal(
        frame, {(player.x + 0.5) * terrain::kChunkSizeMeters, 0.0,
                (player.z + 0.5) * terrain::kChunkSizeMeters});
    timeline->begin(LoadReason::Spawn, 0.0, 0.0, LoadClock::now(), {false, false, false});
    for (int frameIndex = 0; frameIndex < 1000 && timeline->active(); ++frameIndex) {
        streamer.generatePending(3);
        const terrain::unreal::StreamerUpdate update = streamer.update(position, frame);
        for (const terrain::unreal::TileUpload& upload : update.uploads) {
            timeline->recordShown(upload.key, LoadClock::now(), 0.0, upload.collision);
        }
        timeline->recordFrame(1.0, update.uploads.size());
        timeline->observe(terrain::unreal::selectedTiles(streamer, player), LoadClock::now());
    }
    TEST_CHECK(timeline->complete());
    const LoadSummary summary = timeline->summary();
    TEST_CHECK(summary.tiles == streamer.manager().selection().size());
    TEST_CHECK(summary.generatedAgain == 0 && summary.mostUploadsPerFrame <= 4);
    double previous = 0.0;
    for (std::size_t band = 0; band < summary.bandTiles.size(); ++band) {
        if (summary.bandTiles[band] > 0) {
            TEST_CHECK(summary.bandShown[band] >= previous);
            previous = summary.bandShown[band];
        }
    }
    for (const terrain::unreal::TileTrace& trace : timeline->traces()) {
        TEST_CHECK(trace.generations == 1 && trace.shown >= trace.generationEnd);
    }
}

// The horizon is around a point once its finest level covers it: not before the first commit,
// not 10 km away (the finest window of the small levels is 512 m wide), at once on another image.
void testHorizonAround() {
    const horizon_test::World world;
    const terrain::HorizonParams params = horizon_test::smallParams();
    const terrain::HorizonSampler sampler(world.field, world.ecology, params);
    terrain::HorizonClipmap clipmap(params);
    const double x = horizon_test::kX;
    const double z = horizon_test::kZ;
    TEST_CHECK(!terrain::unreal::horizonAround(clipmap, x, z));
    horizon_test::settle(clipmap, sampler, x, z);
    TEST_CHECK(terrain::unreal::horizonAround(clipmap, x, z));
    TEST_CHECK(terrain::unreal::horizonAround(clipmap, x + 200.0, z - 200.0));
    TEST_CHECK(!terrain::unreal::horizonAround(clipmap, x + 10'000.0, z));
    const double worldX = world.field.world().x;
    TEST_CHECK(terrain::unreal::horizonAround(clipmap, x + worldX, z));
    horizon_test::settle(clipmap, sampler, x + 10'000.0, z);
    TEST_CHECK(terrain::unreal::horizonAround(clipmap, x + 10'000.0, z));
    TEST_CHECK(!terrain::unreal::horizonAround(clipmap, x, z));
}

// The distance of a jump across the edges of the wrapped world: a turn of the world is no jump.
void testWrappedDistance() {
    const terrain::WorldSize world;
    const terrain::ChunkCoord count = terrain::worldChunks(world);
    using terrain::unreal::wrappedChunkDistance;
    TEST_CHECK(wrappedChunkDistance({0, 0}, {0, 0}, world) == 0);
    TEST_CHECK(wrappedChunkDistance({3, -5}, {-2, 1}, world) == 6);
    TEST_CHECK(wrappedChunkDistance({0, 0}, {count.x, -count.z}, world) == 0);
    TEST_CHECK(wrappedChunkDistance({count.x / 2 - 1, 0}, {-count.x / 2 + 1, 0}, world) == 2);
    TEST_CHECK(wrappedChunkDistance({0, 0}, {count.x / 2, 7}, world) == count.x / 2);
}

} // namespace

int main() {
    testMilestonesByHand();
    testBandsAndCsv();
    testRecordsFromThreads();
    testOutsideALoad();
    testStreamerLoad();
    testHorizonAround();
    testWrappedDistance();
    return test::result();
}

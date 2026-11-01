// The page of the performance of F3 and the images of Terrain.Shot and Terrain.Views (Bridge):
// the sliding window of the frames and of the tiles, the totals of the timings that the
// summaries of the log never reset, the wait for the streaming to settle (ready, steady,
// timeout), and the names and the lines of the files of a run of views.
#include "dev_perf.hpp"
#include "shot_waiter.hpp"
#include "tile_timings.hpp"

#include "test_assert.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

namespace {

using terrain::unreal::PerfWindow;
using terrain::unreal::ShotWait;
using terrain::unreal::StreamingState;
using terrain::unreal::TileStage;
using terrain::unreal::TileTimings;

bool near(double a, double b, double tolerance = 1e-9) {
    return std::abs(a - b) <= tolerance;
}

void testTotals() {
    TileTimings timings;
    timings.record(TileStage::Generation, 0, 2.0);
    timings.recordMesh(0, 100, 50);
    timings.recordWater(1, 0.5);
    (void)timings.take(); // the summary of the log
    timings.record(TileStage::Generation, 0, 4.0);
    const terrain::unreal::TimingsReport totals = timings.totals();
    TEST_CHECK(totals[0].stage(TileStage::Generation).tiles == 2);
    TEST_CHECK(totals[0].stage(TileStage::Generation).totalMilliseconds == 6.0);
    TEST_CHECK(totals[0].meshes == 1 && totals[1].water.tiles == 1);
    TEST_CHECK(totals[1].water.totalMilliseconds == 0.5);
    TEST_CHECK(timings.snapshot()[0].stage(TileStage::Generation).tiles == 1);
    TEST_CHECK(timings.snapshot()[1].water.tiles == 0);
    // The summaries of the log are unchanged: water meshes are not among their stages.
    TEST_CHECK(terrain::unreal::describe(timings.snapshot()).size() == 1);
}

void testFrames() {
    PerfWindow window(1.0);
    for (int i = 0; i < 100; ++i) { // 2 s of frames, the last second of 20 ms ones kept
        const double ms = i < 40 ? 50.0 : 20.0;
        window.addFrame(i * 0.02, {ms, ms / 2.0, ms / 4.0, i == 99 ? 30.0 : ms / 5.0});
    }
    const terrain::unreal::PerfSummary s = window.summary();
    TEST_CHECK(near(s.frame.mean, 20.0) && near(s.framesPerSecond, 50.0));
    TEST_CHECK(near(s.game.mean, 10.0) && near(s.render.max, 5.0));
    TEST_CHECK(near(s.gpu.max, 30.0) && s.gpu.mean > 4.0 && s.gpu.mean < 4.6);
    TEST_CHECK(s.seconds == 0.0 && s.tilesPerSecond[0] == 0.0); // no totals of tiles yet
    window.clear();
    TEST_CHECK(window.summary().framesPerSecond == 0.0);
    bool threw = false;
    try {
        PerfWindow none(0.0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    TEST_CHECK(threw);
}

void testTiles() {
    PerfWindow window(5.0);
    TileTimings timings;
    for (int second = 0; second <= 12; ++second) {
        // 10 tiles of LOD 0 per second, at 3 ms; from 6 s on, 4 ms; one water tile each second.
        for (int t = 0; t < 10 && second > 0; ++t) {
            timings.record(TileStage::Generation, 0, second > 6 ? 4.0 : 3.0);
            timings.record(TileStage::Upload, 0, 1.0);
        }
        if (second > 0) {
            timings.recordWater(0, 0.25);
        }
        window.addTiles(second, timings.totals());
    }
    const terrain::unreal::PerfSummary s = window.summary();
    TEST_CHECK(near(s.seconds, 5.0));
    TEST_CHECK(near(s.tilesPerSecond[0], 10.0) && near(s.generationMs[0], 4.0));
    TEST_CHECK(near(s.uploadMs[0], 1.0) && s.tilesPerSecond[1] == 0.0);
    TEST_CHECK(s.waterTiles == 5 && near(s.waterMs, 0.25));
    const auto lines = terrain::unreal::describe(s);
    TEST_CHECK(lines.size() == 7);
    TEST_CHECK(lines[2] == "LOD 0: 10.0 tiles/s, generation 4.00 ms, conversion 0.00 ms, upload "
                           "1.00 ms");
    TEST_CHECK(lines[6] == "water meshes: 5 tiles, 0.25 ms each, tiles over the last 5.0 s "
                           "(window 5 s)");
}

StreamingState settledState() {
    StreamingState state;
    state.selectedTiles = 200;
    state.shownTiles = 200;
    return state;
}

void testWaiter() {
    using terrain::unreal::settled;
    StreamingState state = settledState();
    TEST_CHECK(settled(state));
    std::vector<StreamingState> busy(5, state);
    busy[0].pendingTiles = 1;
    busy[1].shownTiles = 199;
    busy[2].horizonBusy = true;
    busy[3].horizonShown = false;
    busy[4].editsPending = 2;
    for (const StreamingState& not_settled : busy) {
        TEST_CHECK(!settled(not_settled));
    }
    terrain::unreal::ShotWaiter waiter({0.5, 1.0, 10.0});
    StreamingState loading = state;
    loading.pendingTiles = 12;
    TEST_CHECK(waiter.update(0.0, loading) == ShotWait::Waiting);
    TEST_CHECK(waiter.update(2.0, state) == ShotWait::Waiting);  // settled since 2 s
    TEST_CHECK(waiter.update(2.9, state) == ShotWait::Waiting);
    TEST_CHECK(waiter.update(3.1, loading) == ShotWait::Waiting); // a tile again: from zero
    TEST_CHECK(waiter.update(3.5, state) == ShotWait::Waiting);
    TEST_CHECK(waiter.update(4.6, state) == ShotWait::Ready);
    // Settled at once, still not before leastSeconds nor before settleSeconds.
    terrain::unreal::ShotWaiter quick({0.5, 0.0, 10.0});
    TEST_CHECK(quick.update(0.1, state) == ShotWait::Waiting);
    TEST_CHECK(quick.update(0.6, state) == ShotWait::Ready);
    terrain::unreal::ShotWaiter slow({0.5, 1.0, 3.0});
    TEST_CHECK(slow.update(2.9, loading) == ShotWait::Waiting);
    TEST_CHECK(slow.update(3.0, loading) == ShotWait::TimedOut);
    bool threw = false;
    try {
        terrain::unreal::ShotWaiter wrong({5.0, 1.0, 3.0});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    TEST_CHECK(threw);
}

void testNames() {
    TEST_CHECK(terrain::unreal::viewsFolderName(2026, 10, 31, 9, 5, 7) == "2026-10-31_09-05-07");
    TEST_CHECK(terrain::unreal::viewFileName(1, "chute388_bassin") == "01_chute388_bassin.png");
    TEST_CHECK(terrain::unreal::viewFileName(123, "k2") == "123_k2.png");
    TEST_CHECK(terrain::unreal::viewsHeader(20261019, "surface", "", "4/8/16/32") ==
               "# Terrain.Views: seed 20261019, preset surface, planet file none, LOD rings "
               "4/8/16/32\n");
    terrain::unreal::ViewRecord record;
    record.file = "01_chute388_bassin.png";
    record.place.name = "chute388_bassin";
    record.pose.eye = {-6185104.0, 182.03, 155924.0};
    record.pose.yaw = 137.0;
    record.pose.pitch = 30.0;
    record.waitedSeconds = 7.44;
    record.ready = true;
    record.state = settledState();
    record.framesPerSecond = 58.06;
    TEST_CHECK(terrain::unreal::describeView(record) ==
               "01_chute388_bassin.png chute388_bassin x -6185104.00 z 155924.00 eye 182.03 m "
               "yaw 137.00 pitch 30.00: ready after 7.4 s, 0 tiles pending, 200/200 shown, "
               "horizon done, 58.1 fps");
    record.ready = false;
    record.planetMissing = true;
    record.state.horizonBusy = true;
    const std::string line = terrain::unreal::describeView(record);
    TEST_CHECK(line.find("NOT READY (timeout)") != std::string::npos);
    TEST_CHECK(line.find("horizon busy") != std::string::npos);
    TEST_CHECK(line.ends_with(", WITHOUT the planet file it needs"));
}

// How an image was taken, in views.txt: the camera read back against the pose, the sizes; and
// the two faults it shows: a camera turned away, an image of the top left of the view only (a
// display scaled at 200 % by Windows, the viewport counted at the scale of the desktop).
void testCaptureCheck() {
    terrain::unreal::ViewRecord record;
    record.file = "02_fleuve_plaine.png";
    record.place.name = "fleuve_plaine";
    record.pose.yaw = -85.0;
    record.pose.pitch = 0.0;
    record.state = settledState();
    TEST_CHECK(terrain::unreal::captureWarnings(record).empty()); // not measured
    terrain::unreal::CaptureCheck& c = record.capture;
    c.measured = true;
    c.camera.yaw = 275.0 - 360.0; // the same yaw, read as Unreal gives it
    c.camera.pitch = 0.2;
    c.fov = 90.0;
    c.viewportWidth = 1920;
    c.viewportHeight = 1080;
    c.textureWidth = 3840;
    c.textureHeight = 2160;
    c.imageWidth = 3840;
    c.imageHeight = 2160;
    c.method = "texture";
    TEST_CHECK(terrain::unreal::captureWarnings(record).empty());
    TEST_CHECK(terrain::unreal::describeView(record).ends_with(
        "; camera yaw -85.00 pitch 0.20 fov 90.0, image 3840x2160 (viewport 1920x1080, texture "
        "3840x2160, read from the texture)"));
    c.imageWidth = 1920; // the top left only
    c.imageHeight = 1080;
    c.camera.pitch = 25.0;
    c.camera.yaw = 179.0;
    record.pose.yaw = -179.0; // 2 degrees apart across the end of (-180, 180]
    const std::vector<std::string> warnings = terrain::unreal::captureWarnings(record);
    TEST_CHECK(warnings.size() == 2);
    const std::string line = terrain::unreal::describeView(record);
    TEST_CHECK(line.find("; WARNING: the camera was not at the pose asked") != std::string::npos);
    TEST_CHECK(line.find("; WARNING: the image is not the size of the view") != std::string::npos);
    c.camera.pitch = 0.0;
    c.camera.yaw = -179.4;
    c.imageWidth = 0; // not known: no warning on the size
    TEST_CHECK(terrain::unreal::captureWarnings(record).empty());
}

// The size of a PNG from its header: a real one, cut short, not a PNG.
void testPngSize() {
    const std::vector<std::uint8_t> header = {
        0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R',
        0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x08, 0x70, 8, 6, 0, 0, 0};
    const auto size = terrain::unreal::pngSize(header);
    TEST_CHECK(size && size->first == 3840 && size->second == 2160);
    TEST_CHECK(!terrain::unreal::pngSize(std::span(header).first(23)));
    std::vector<std::uint8_t> jpeg = header;
    jpeg[1] = 'J';
    TEST_CHECK(!terrain::unreal::pngSize(jpeg));
    std::vector<std::uint8_t> chunk = header;
    chunk[12] = 'X'; // not the IHDR chunk first
    TEST_CHECK(!terrain::unreal::pngSize(chunk));
}

} // namespace

int main() {
    testTotals();
    testFrames();
    testTiles();
    testWaiter();
    testNames();
    testCaptureCheck();
    testPngSize();
    return test::result();
}

// Timings of the tiles in the Unreal plugin: counts, means and maxima per stage and LOD, mesh
// sizes, the reset of take, concurrent records, the timed generator, and the summary lines.
#include "tile_timings.hpp"

#include "test_assert.hpp"

#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using terrain::TileData;
using terrain::TileRequest;
using terrain::unreal::TileStage;
using terrain::unreal::TileTimings;
using terrain::unreal::TimingsReport;

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

void testStats() {
    TileTimings timings;
    timings.record(TileStage::Generation, 0, 2.0);
    timings.record(TileStage::Generation, 0, 4.0);
    timings.record(TileStage::Upload, 3, 1.5);
    timings.recordMesh(0, 100, 50);
    timings.recordMesh(0, 300, 150);
    const TimingsReport report = timings.snapshot();
    const terrain::unreal::StageStats& generation = report[0].stage(TileStage::Generation);
    TEST_CHECK(generation.tiles == 2);
    TEST_CHECK(generation.totalMilliseconds == 6.0);
    TEST_CHECK(generation.meanMilliseconds() == 3.0);
    TEST_CHECK(generation.maxMilliseconds == 4.0);
    TEST_CHECK(report[0].stage(TileStage::Conversion).tiles == 0);
    TEST_CHECK(report[0].stage(TileStage::Conversion).meanMilliseconds() == 0.0);
    TEST_CHECK(report[3].stage(TileStage::Upload).tiles == 1);
    TEST_CHECK(report[0].meshes == 2 && report[0].vertices == 400 && report[0].triangles == 200);
    TEST_CHECK(report[1].meshes == 0 && report[2].stage(TileStage::Generation).tiles == 0);
}

void testTakeResets() {
    TileTimings timings;
    timings.record(TileStage::Conversion, 2, 0.5);
    const TimingsReport taken = timings.take();
    TEST_CHECK(taken[2].stage(TileStage::Conversion).tiles == 1);
    const TimingsReport after = timings.snapshot();
    TEST_CHECK(after[2].stage(TileStage::Conversion).tiles == 0);
    TEST_CHECK(terrain::unreal::describe(after).empty());
}

bool throwsInvalid(void (*action)(TileTimings&)) {
    TileTimings timings;
    try {
        action(timings);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

void testInvalidLod() {
    TEST_CHECK(throwsInvalid([](TileTimings& t) { t.record(TileStage::Upload, -1, 1.0); }));
    TEST_CHECK(throwsInvalid([](TileTimings& t) { t.record(TileStage::Upload, 4, 1.0); }));
    TEST_CHECK(throwsInvalid([](TileTimings& t) { t.recordMesh(4, 1, 1); }));
    TEST_CHECK(!throwsInvalid([](TileTimings& t) { t.record(TileStage::Upload, 3, 1.0); }));
}

// Workers record their generations concurrently.
void testConcurrentRecords() {
    TileTimings timings;
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&timings] {
            for (int i = 0; i < 1000; ++i) {
                timings.record(TileStage::Generation, 1, 1.0);
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    const terrain::unreal::StageStats stats = timings.snapshot()[1].stage(TileStage::Generation);
    TEST_CHECK(stats.tiles == 4000);
    TEST_CHECK(stats.totalMilliseconds == 4000.0);
}

TileData fakeTile(const TileRequest& request) {
    TileData data;
    data.request = request;
    data.originX = request.key.origin.x * terrain::kChunkSizeMeters;
    data.mesh.vertices.push_back({1.0f, 2.0f, 3.0f});
    return data;
}

// The timed generator records each generation and gives the tile of the generator.
void testTimedGenerator() {
    const auto timings = std::make_shared<TileTimings>();
    const terrain::TileGenerator slow = [](const TileRequest& request) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return fakeTile(request);
    };
    const terrain::TileGenerator timed = terrain::unreal::timedGenerator(slow, timings);
    const TileRequest request{{{3, -4}, 2}, terrain::kSeamNegX};
    TEST_CHECK(timed(request) == fakeTile(request));
    const terrain::unreal::StageStats stats = timings->snapshot()[2].stage(TileStage::Generation);
    TEST_CHECK(stats.tiles == 1);
    TEST_CHECK(stats.maxMilliseconds >= 4.9);
    bool rejected = false;
    try {
        static_cast<void>(terrain::unreal::timedGenerator(slow, nullptr));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    TEST_CHECK(rejected);
}

// One line per LOD holding measures, with their numbers.
void testDescribe() {
    TileTimings timings;
    timings.record(TileStage::Generation, 0, 2.0);
    timings.record(TileStage::Generation, 0, 4.0);
    timings.record(TileStage::Upload, 0, 1.25);
    timings.recordMesh(0, 400, 200);
    timings.record(TileStage::Conversion, 3, 0.5);
    const std::vector<std::string> lines = terrain::unreal::describe(timings.snapshot());
    TEST_CHECK(lines.size() == 2);
    if (lines.size() == 2) {
        TEST_CHECK(lines[0].starts_with("LOD 0:"));
        TEST_CHECK(contains(lines[0], "generation 2 tiles 3.00 ms (max 4.00)"));
        TEST_CHECK(contains(lines[0], "upload 1 tiles 1.25 ms"));
        TEST_CHECK(contains(lines[0], "400 vertices 200 triangles per mesh"));
        TEST_CHECK(lines[1].starts_with("LOD 3:"));
        TEST_CHECK(contains(lines[1], "conversion 1 tiles 0.50 ms"));
    }
}

} // namespace

int main() {
    testStats();
    testTakeResets();
    testInvalidLod();
    testConcurrentRecords();
    testTimedGenerator();
    testDescribe();
    return test::result();
}

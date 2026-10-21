// Coverage and cost of the far horizon. Asserted: the default levels cover at least 300 km
// around any observer. Printed only, never asserted: the time of a full computation of every
// level on one thread and on every hardware thread, of the updates of a walk, the memory, and
// the same with the slopes of the materials at the pitch of each level and with K = 512.
#include "terrain/streaming/horizon_streamer.hpp"

#include "horizon_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <optional>
#include <thread>
#include <utility>

namespace {

using horizon_test::kX;
using horizon_test::kZ;
using horizon_test::World;

class Stopwatch {
public:
    [[nodiscard]] double seconds() const {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    }

private:
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

int hardwareThreads() { return std::max(1, static_cast<int>(std::thread::hardware_concurrency())); }

void testCoverage() {
    const terrain::HorizonParams params;
    TEST_CHECK(terrain::guaranteedRadius(params) >= 300'000.0);
    const int last = params.levels - 1;
    // The window of the coarsest level, wherever the observer stands.
    for (const auto& [x, z] : {std::pair{kX, kZ}, std::pair{params.world.x / 2.0 - 1.0, 0.0}}) {
        const terrain::HorizonIndex center = terrain::canonicalCenter(params, last, x, z);
        terrain::HorizonLevel level(params, last);
        level.moveTo(center);
        const double side = params.cellsPerSide * static_cast<double>(level.pitch());
        TEST_CHECK(level.originX(x) <= x - 300'000.0 && level.originX(x) + side >= x + 300'000.0);
        TEST_CHECK(level.originZ(z) <= z - 300'000.0 && level.originZ(z) + side >= z + 300'000.0);
    }
    std::printf("levels: %d, %d x %d samples each, pitches %d m to %d m, radius %.0f km\n",
                params.levels, terrain::samplesPerSide(params), terrain::samplesPerSide(params),
                terrain::levelPitch(params, 0), terrain::levelPitch(params, last),
                terrain::guaranteedRadius(params) / 1000.0);
}

// A full computation on the calling thread, then the updates of a walk of 2 km.
void measureOneThread(const World& world, const terrain::HorizonParams& params) {
    const terrain::HorizonSampler sampler(world.field, world.ecology, params);
    terrain::HorizonClipmap clipmap(params);
    const Stopwatch full;
    const std::int64_t samples = horizon_test::settle(clipmap, sampler, kX, kZ);
    const double fullSeconds = full.seconds();
    std::printf("full computation, 1 thread: %.2f s for %lld samples (%.2f us each), %.1f MB\n",
                fullSeconds, static_cast<long long>(samples),
                1e6 * fullSeconds / static_cast<double>(samples),
                static_cast<double>(clipmap.memoryBytes()) / 1e6);
    double total = 0.0;
    double worst = 0.0;
    std::int64_t walked = 0;
    constexpr int kSteps = 250; // 8 m each: one sample of level 0
    for (int step = 1; step <= kSteps; ++step) {
        const Stopwatch update;
        walked += horizon_test::settle(clipmap, sampler, kX + 8.0 * step, kZ);
        total += update.seconds();
        worst = std::max(worst, update.seconds());
    }
    std::printf("walk of 2 km in steps of 8 m, 1 thread: %.2f ms per step on average, %.2f ms at "
                "most, %lld samples\n",
                1e3 * total / kSteps, 1e3 * worst, static_cast<long long>(walked));
}

// A full computation by the workers of a streamer.
void measureWorkers(const World& world, const terrain::HorizonParams& params, const char* name) {
    const terrain::HorizonSampler sampler(world.field, world.ecology, params);
    const int threads = hardwareThreads();
    terrain::HorizonStreamer streamer(sampler, {threads, terrain::kHorizonPieceSamples});
    const Stopwatch full;
    horizon_test::settle(streamer, kX, kZ);
    std::printf("full computation, %s, %d threads: %.2f s, %.1f MB\n", name, threads,
                full.seconds(), static_cast<double>(streamer.clipmap().memoryBytes()) / 1e6);
}

} // namespace

int main() {
    testCoverage();
    const World world;
    const terrain::HorizonParams defaults;
    measureOneThread(world, defaults);
    measureWorkers(world, defaults, "default (K = 256)");
    terrain::HorizonParams coarseSlopes;
    coarseSlopes.materialSlopeStep = 0;
    measureWorkers(world, coarseSlopes, "material slopes at the pitch of each level");
    terrain::HorizonParams wide;
    wide.cellsPerSide = 512;
    measureWorkers(world, wide, "K = 512");
    return test::result();
}

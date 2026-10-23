// Coverage and cost of the far horizon. Asserted: the default levels cover at least 300 km
// around any observer. Printed only, never asserted: the time of a full computation of every
// level on one thread and on every hardware thread, of the updates of a walk, the memory, and
// the same with the slopes of the materials at the pitch of each level and with K = 512; the
// detail slopes around the K2, timed on one piece in kDetailStride and extrapolated (in the
// mountain cores of the default world, the full computation would take minutes).
#include "terrain/streaming/horizon_normals.hpp"
#include "terrain/streaming/horizon_streamer.hpp"

#include "horizon_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

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

// The detail slopes are timed on one piece in kDetailStride, taken in order through the levels.
constexpr std::size_t kDetailStride = 8;

// The time of the detail slopes of every window around (x, z), in pieces of 16 rows of samples on
// `threads` threads, the levels whose index is at least `fromLevel` only: extrapolated from the
// pieces timed, by their share of the texels.
double detailSeconds(const terrain::HorizonNormalSampler& sampler, double x, double z, int threads,
                     int fromLevel) {
    const terrain::HorizonParams& params = sampler.params();
    std::vector<terrain::HorizonRect> pieces;
    std::vector<int> levelOf;
    double texels = 0.0;
    double timedTexels = 0.0;
    std::size_t index = 0;
    for (int level = fromLevel; level < params.levels; ++level) {
        const terrain::HorizonRect window = terrain::windowAround(
            terrain::canonicalCenter(params, level, x, z), params.cellsPerSide);
        for (std::int64_t k = window.z0; k < window.z1; k += 16, ++index) {
            const terrain::HorizonRect piece{window.x0, k, window.x1,
                                             std::min<std::int64_t>(k + 16, window.z1)};
            const auto count = static_cast<double>(sampler.texelCount(piece));
            texels += count;
            if (index % kDetailStride == 0) {
                pieces.push_back(piece);
                levelOf.push_back(level);
                timedTexels += count;
            }
        }
    }
    const Stopwatch watch;
    std::vector<std::thread> workers;
    for (int t = 0; t < threads; ++t) {
        workers.emplace_back([&, t] {
            std::vector<terrain::DetailSlope> out;
            for (std::size_t n = static_cast<std::size_t>(t); n < pieces.size();
                 n += static_cast<std::size_t>(threads)) {
                out.resize(static_cast<std::size_t>(sampler.texelCount(pieces[n])));
                sampler.fill(levelOf[n], pieces[n], out);
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
    return watch.seconds() * texels / timedTexels;
}

void measureDetail(const World& world) {
    constexpr double kK2X = 416'857.0;
    constexpr double kK2Z = -11'398'966.0;
    const terrain::HorizonParams params;
    for (const int ratio : {2, 4, 8}) {
        const terrain::HorizonNormalSampler sampler(world.field, params, ratio);
        const double texels = static_cast<double>(params.levels) *
                              terrain::samplesPerSide(params) * terrain::samplesPerSide(params) *
                              ratio * ratio;
        const double one = detailSeconds(sampler, kK2X, kK2Z, 1, 0);
        std::printf("detail slopes, ratio %d, every level around the K2: %.2f s on 1 thread "
                    "(%.2f us per texel), %.2f s on 2, %.2f s on %d; %.1f MB as half floats "
                    "(times extrapolated from one piece in %zu)\n",
                    ratio, one, 1e6 * one / texels, detailSeconds(sampler, kK2X, kK2Z, 2, 0),
                    detailSeconds(sampler, kK2X, kK2Z, hardwareThreads(), 0), hardwareThreads(),
                    texels * 4.0 / 1e6, kDetailStride);
    }
    const terrain::HorizonNormalSampler sampler(world.field, params, 4);
    std::printf("detail slopes, ratio 4, levels 5 and up only, 2 threads: %.2f s (extrapolated "
                "from one piece in %zu)\n",
                detailSeconds(sampler, kK2X, kK2Z, 2, 5), kDetailStride);
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
    measureDetail(world);
    return test::result();
}

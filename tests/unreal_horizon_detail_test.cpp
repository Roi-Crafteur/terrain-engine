// The crests of the GPU horizon computed after the epochs (HorizonGpuParams::deferCrests), around
// mountains: once the detail is in, the simulated textures and placements equal those of the
// crests computed in the epochs, bit for bit, at the crest ratios 2 and 4 (detail normals at 2 and
// 4: one grid), after a walk and a jump of 30 km; with the detail of earlier epochs computed after
// samples left the windows and came back (no crest lost or lifted twice); without detail normals
// (busy() settles them); on a pool of 2 threads; the clipmap keeps the point samples; the tiles of
// the jobs cover their rectangles once.
#include "horizon_gpu_streamer.hpp"

#include "horizon_test_support.hpp"
#include "legacy_relief_support.hpp"
#include "test_assert.hpp"
#include "unreal_horizon_gpu_support.hpp"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <set>
#include <utility>
#include <vector>

namespace {

using gpu_test::Gpu;
using terrain::HorizonRect;
using terrain::unreal::HorizonGpuParams;
using terrain::unreal::HorizonGpuStreamer;
using terrain::unreal::JobLauncher;
using terrain::unreal::UnrealFrame;
using terrain::unreal::UnrealPoint;

const terrain::WorldSize kWorld;
const UnrealFrame kFrame{{horizon_test::kX, 1234.5, horizon_test::kZ}};

// 4 levels of 128 cells, tiles within 23 chunks.
HorizonGpuParams paramsOf(int normalRatio, int crestRatio, bool deferred) {
    HorizonGpuParams params;
    params.levels = 4;
    params.cellsPerSide = 128;
    params.lod = {{1, 3, 7}, 23};
    params.normalRatio = normalRatio;
    params.crestRatio = crestRatio;
    params.deferCrests = deferred;
    return params;
}

// A streamer around the mountains of the legacy relief and what its GPU holds.
struct Session {
    Session(const horizon_test::World& world, const HorizonGpuParams& params, JobLauncher launch)
        : streamer(world.field, world.ecology, kWorld, params, std::move(launch)) {}

    HorizonGpuStreamer streamer;
    Gpu gpu{129};

    // Updates until nothing is left to do, the jobs run by `run` between the updates.
    void settle(const UnrealPoint& player, const std::function<void()>& run) {
        for (int n = 0; n < 10'000; ++n) {
            gpu.apply(streamer.update(player, kFrame));
            if (!streamer.busy()) {
                return;
            }
            run();
        }
        TEST_CHECK(false);
    }
};

void runInPlace() {}

JobLauncher inPlace() {
    return [](std::function<void()> job) { job(); };
}

// The samples of the clipmap whose texel shows them raised: the clipmap keeps the point samples.
int raisedTexels(const Session& session) {
    int raised = 0;
    const terrain::HorizonClipmap& clipmap = session.streamer.clipmap();
    for (int n = 1; n < clipmap.levelCount(); ++n) {
        for (int k = 0; k < 129; ++k) {
            for (int i = 0; i < 129; ++i) {
                const float shown = session.gpu.texel(n, i, k).height;
                raised += shown > clipmap.level(n).atWindow(i, k).height ? 1 : 0;
            }
        }
    }
    return raised;
}

// Crests after the epochs (normals at the crest ratio) against crests in the epochs (no normal).
void testSameAsEpochs(const horizon_test::World& world, int crestRatio, bool jump) {
    Session deferred(world, paramsOf(crestRatio, crestRatio, true), inPlace());
    Session epochs(world, paramsOf(0, crestRatio, false), inPlace());
    const terrain::HorizonSampler reference(world.field, world.ecology,
                                            epochs.streamer.params());
    UnrealPoint player{0.0, 0.0, 0.0};
    std::vector<double> steps{0.0, 1500.0, 9000.0, 33'000.0};
    if (jump) {
        steps.push_back(3'000'000.0);
    }
    bool same = true;
    for (const double step : steps) {
        player.x += step;
        player.y -= 0.6 * step;
        deferred.settle(player, runInPlace);
        epochs.settle(player, runInPlace);
        same = same && deferred.gpu.sameView(epochs.gpu);
    }
    TEST_CHECK(same);
    TEST_CHECK(gpu_test::texelsMatch(deferred.gpu, deferred.streamer.clipmap(), reference));
    const int raised = raisedTexels(deferred);
    std::printf("crest ratio %d after the epochs: the same texels as in the epochs, %d raised over "
                "the point samples of the clipmap\n",
                crestRatio, raised);
    TEST_CHECK(raised > 0 && deferred.streamer.clipmap().params().crestRatio == 0);
    TEST_CHECK(deferred.streamer.params().crestRatio == crestRatio);
}

// The jobs run one at a time, in their order, until the window of level 0 moves.
void moveTo(Session& session, gpu_test::Jobs& jobs, const UnrealPoint& player) {
    const HorizonRect before = session.streamer.clipmap().level(0).window();
    for (int n = 0; n < 10'000 && session.streamer.clipmap().level(0).window() == before; ++n) {
        session.gpu.apply(session.streamer.update(player, kFrame));
        if (session.streamer.clipmap().level(0).window() == before && !jobs.queue.empty()) {
            jobs.queue.front()();
            jobs.queue.pop_front();
        }
    }
}

// Away and back while the detail of the earlier epochs waits: it is computed for samples that
// left their windows and came back, written again by later epochs.
void testStaleJobs(const horizon_test::World& world) {
    gpu_test::Jobs jobs;
    Session deferred(world, paramsOf(2, 2, true), jobs.launcher());
    deferred.settle({0.0, 0.0, 0.0}, [&] { jobs.runAll(); });
    moveTo(deferred, jobs, {40'000.0, 0.0, 0.0});
    const std::size_t waiting = jobs.queue.size();
    moveTo(deferred, jobs, {0.0, 0.0, 0.0});
    moveTo(deferred, jobs, {-40'000.0, 30'000.0, 0.0});
    moveTo(deferred, jobs, {0.0, 0.0, 0.0});
    deferred.settle({0.0, 0.0, 0.0}, [&] { jobs.runAll(); });
    Session epochs(world, paramsOf(0, 2, false), inPlace());
    epochs.settle({0.0, 0.0, 0.0}, runInPlace);
    TEST_CHECK(waiting > 0);
    TEST_CHECK(deferred.gpu.sameView(epochs.gpu));
}

// Crests without detail normals: no normal stream, the crests settled by busy().
void testWithoutNormals(const horizon_test::World& world) {
    gpu_test::Jobs jobs;
    Session deferred(world, paramsOf(0, 4, true), jobs.launcher());
    TEST_CHECK(deferred.streamer.normals() == nullptr && deferred.streamer.detail() != nullptr);
    deferred.settle({0.0, 0.0, 0.0}, [&] { jobs.runAll(); });
    Session epochs(world, paramsOf(0, 4, false), inPlace());
    epochs.settle({0.0, 0.0, 0.0}, runInPlace);
    TEST_CHECK(epochs.streamer.detail() == nullptr);
    TEST_CHECK(deferred.gpu.sameView(epochs.gpu));
}

// The epochs and the detail on 2 threads.
void testThreads(const horizon_test::World& world) {
    gpu_test::Pool pool(2);
    HorizonGpuParams params = paramsOf(2, 2, true);
    params.maxPiecesInFlight = 2;
    params.normalJobs = 2;
    Session deferred(world, params, pool.launcher());
    deferred.settle({0.0, 0.0, 0.0}, [&] { deferred.streamer.waitIdle(); });
    Session epochs(world, paramsOf(0, 2, false), inPlace());
    epochs.settle({0.0, 0.0, 0.0}, runInPlace);
    TEST_CHECK(deferred.gpu.sameView(epochs.gpu));
}

// The tiles of the jobs cover their rectangle once, about as square as it allows.
void testTiles() {
    for (const HorizonRect& rect : {HorizonRect{-3, 5, 254, 262}, HorizonRect{0, 0, 2, 257},
                                    HorizonRect{0, 0, 257, 2}, HorizonRect{4, 4, 261, 20},
                                    HorizonRect{0, 0, 1, 1}}) {
        std::set<std::pair<std::int64_t, std::int64_t>> covered;
        std::int64_t samples = 0;
        std::int64_t largest = 0;
        for (const HorizonRect& tile : terrain::unreal::detailTiles(rect, 1024)) {
            samples += tile.count();
            largest = std::max(largest, tile.count());
            for (std::int64_t k = tile.z0; k < tile.z1; ++k) {
                for (std::int64_t i = tile.x0; i < tile.x1; ++i) {
                    covered.insert({i, k});
                }
            }
        }
        TEST_CHECK(samples == rect.count() && static_cast<std::int64_t>(covered.size()) == samples);
        TEST_CHECK(largest <= 1024 + 2 * 1024 / 32);
    }
    const std::vector<HorizonRect> square = terrain::unreal::detailTiles({0, 0, 257, 257}, 1024);
    TEST_CHECK(square.size() == 81u && square.front().width() == 28 &&
               square.front().height() == 28);
    TEST_CHECK(terrain::unreal::detailTiles({0, 0, 2, 257}, 1024).size() == 1u);
    TEST_CHECK(terrain::unreal::detailTiles({0, 0, 257, 2}, 1024).size() == 1u);
    TEST_CHECK(terrain::unreal::detailTiles({}, 1024).empty());
}

} // namespace

int main() {
    try {
        const horizon_test::World world(test_relief::legacyRelief());
        testTiles();
        testSameAsEpochs(world, 2, true);
        testSameAsEpochs(world, 4, false);
        testStaleJobs(world);
        testWithoutNormals(world);
        testThreads(world);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "exception: %s\n", e.what());
        TEST_CHECK(false);
    }
    return test::result();
}

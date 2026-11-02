// The GPU horizon brought to a new place level by level, finest first (agent 38,
// HorizonGpuParams::progressive): one more level shown at each commit, the coarsest shown without
// a coarser level to morph toward, the others hidden; after a jump in the middle of an epoch, the
// epoch abandoned without waiting and the levels of the old place hidden; more pieces at once
// while it runs; and at the end the very texels and placements of a streamer that showed every
// level at once.
#include "horizon_gpu_streamer.hpp"
#include "horizon_test_support.hpp"
#include "test_assert.hpp"
#include "unreal_horizon_gpu_support.hpp"

#include <cstddef>
#include <functional>
#include <vector>

namespace {

using terrain::unreal::HorizonGpuParams;
using terrain::unreal::HorizonGpuStreamer;
using terrain::unreal::HorizonGpuUpdate;
using terrain::unreal::LevelPlacement;
using terrain::unreal::UnrealFrame;
using terrain::unreal::UnrealPoint;

const terrain::WorldSize kWorld;
const UnrealFrame kFrame{{horizon_test::kX, 1234.5, horizon_test::kZ}};
constexpr int kLevels = 5;

// Windows of 128 cells, 5 levels, no detail (point samples only).
HorizonGpuParams params(bool progressive) {
    HorizonGpuParams result;
    result.levels = kLevels;
    result.cellsPerSide = 128;
    result.lod = {{1, 3, 7}, 23};
    result.normalRatio = 0;
    result.crestRatio = 0;
    result.progressive = progressive;
    return result;
}

UnrealPoint at(double x, double z) {
    return terrain::unreal::toUnreal(kFrame, {x, 0.0, z});
}

// Every level at once, jobs run within the update: the reference.
struct Reference {
    horizon_test::World world;
    HorizonGpuStreamer streamer{world.field, world.ecology, kWorld, params(false),
                                [](std::function<void()> job) { job(); }};
    gpu_test::Gpu gpu{129};

    Reference(double x, double z) { gpu.apply(streamer.update(at(x, z), kFrame)); }
};

// Level by level, jobs run when the test says so.
struct Progressive {
    horizon_test::World world;
    gpu_test::Jobs jobs;
    HorizonGpuStreamer streamer{world.field, world.ecology, kWorld, params(true),
                                jobs.launcher()};
    gpu_test::Gpu gpu{129};

    HorizonGpuUpdate update(double x, double z) {
        HorizonGpuUpdate result = streamer.update(at(x, z), kFrame);
        gpu.apply(result);
        return result;
    }
};

bool visibleLevels(const gpu_test::Gpu& gpu, int shown) {
    int visible = 0;
    for (const auto& [level, placement] : gpu.placements()) {
        visible += placement.visible ? 1 : 0;
        if (placement.visible && level >= shown) {
            return false;
        }
        // The coarsest level shown has no coarser one to morph toward.
        const bool coarsest = level + 1 == shown;
        if (placement.visible && coarsest != (placement.inCoarser.x < 0)) {
            return false;
        }
    }
    return visible == shown;
}

// Settles the progressive streamer at a place: returns the levels shown after each commit.
std::vector<int> settle(Progressive& p, double x, double z) {
    std::vector<int> shown;
    for (int round = 0; round < 100; ++round) {
        const HorizonGpuUpdate update = p.update(x, z);
        if (update.committed) {
            shown.push_back(p.streamer.levelsShown());
            TEST_CHECK(visibleLevels(p.gpu, p.streamer.levelsShown()));
        }
        if (!p.streamer.busy()) {
            break;
        }
        p.jobs.runAll();
    }
    return shown;
}

// From nothing: levels 1, 2, ... 5 shown one commit after the other, the coarser ones hidden,
// then the view of the reference.
void testLevelByLevel() {
    Progressive p;
    const double x = horizon_test::kX;
    const double z = horizon_test::kZ;
    TEST_CHECK(p.streamer.levelsShown() == 0);
    const std::vector<int> shown = settle(p, x, z);
    TEST_CHECK(shown == (std::vector<int>{1, 2, 3, 4, 5}));
    const Reference reference(x, z);
    TEST_CHECK(p.gpu.sameView(reference.gpu));
}

// A step of a walk, its epoch in flight, then a jump of 300 km: the levels of the old place
// hidden at once, the epoch abandoned (its pieces in flight end, no other is launched, nothing
// committed), then the new place level by level, as the reference there.
void testJumpAbandons() {
    Progressive p;
    const double x = horizon_test::kX;
    const double z = horizon_test::kZ;
    settle(p, x, z);
    p.update(x + 300.0, z); // a step: an epoch of every level, its pieces queued
    TEST_CHECK(p.streamer.busy() && p.streamer.levelsShown() == kLevels);
    const std::size_t inFlight = p.jobs.queue.size();
    TEST_CHECK(inFlight == 2);
    const double far = x + 300'000.0;
    const HorizonGpuUpdate jump = p.update(far, z);
    TEST_CHECK(!jump.committed && p.streamer.levelsShown() == 0);
    TEST_CHECK(jump.placements.size() == static_cast<std::size_t>(kLevels));
    TEST_CHECK(visibleLevels(p.gpu, 0));
    // The pieces in flight end; none of the epoch is launched after them.
    p.jobs.runAll();
    TEST_CHECK(p.jobs.queue.empty());
    const std::vector<int> shown = settle(p, far, z);
    TEST_CHECK(shown == (std::vector<int>{1, 2, 3, 4, 5}));
    const Reference reference(far, z);
    TEST_CHECK(p.gpu.sameView(reference.gpu));
}

void runOne(gpu_test::Jobs& jobs) {
    std::function<void()> job = std::move(jobs.queue.front());
    jobs.queue.pop_front();
    job();
}

// Fewer pieces at once while an epoch runs: a piece ending launches another only below the limit;
// more: launched at once. The first level has 5 pieces of 31 rows.
void testPiecesInFlight() {
    Progressive p;
    const double x = horizon_test::kX;
    const double z = horizon_test::kZ;
    p.update(x, z);
    TEST_CHECK(p.jobs.queue.size() == 2 && p.streamer.maxPiecesInFlight() == 2);
    p.streamer.setMaxPiecesInFlight(1);
    TEST_CHECK(p.jobs.queue.size() == 2 && p.streamer.maxPiecesInFlight() == 1);
    runOne(p.jobs); // the other still in flight: nothing launched
    TEST_CHECK(p.jobs.queue.size() == 1);
    runOne(p.jobs); // none in flight: the third piece launched
    TEST_CHECK(p.jobs.queue.size() == 1);
    p.streamer.setMaxPiecesInFlight(4); // the last two launched at once
    TEST_CHECK(p.jobs.queue.size() == 3);
    settle(p, x, z);
    const Reference reference(x, z);
    TEST_CHECK(p.gpu.sameView(reference.gpu));
}

} // namespace

int main() {
    testLevelByLevel();
    testJumpAbandons();
    testPiecesInFlight();
    return test::result();
}

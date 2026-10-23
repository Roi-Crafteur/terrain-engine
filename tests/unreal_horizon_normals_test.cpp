// Detail normals of the GPU horizon (Bridge/horizon_normal_texels, Bridge/horizon_normal_stream):
// the half floats (every half back and forth, ties to even, clamping), the regions of a rectangle
// split where the slots wrap, a part of the texels of a rectangle; then, on real levels with
// small windows, the pending texels written with each commit, the detail of every window once
// the jobs ran (equal to HorizonNormalSampler at every texel the material reads), after a walk
// and after a jump that leaves the queued jobs out of their windows, and without the detail.
#include "horizon_gpu_streamer.hpp"
#include "horizon_normal_stream.hpp"
#include "horizon_normal_texels.hpp"

#include "horizon_test_support.hpp"
#include "test_assert.hpp"

#include <bit>
#include <cstdio>
#include <exception>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>

namespace {

using terrain::HorizonRect;
using terrain::unreal::floatFromHalf;
using terrain::unreal::halfFromFloat;
using terrain::unreal::HorizonGpuParams;
using terrain::unreal::HorizonGpuStreamer;
using terrain::unreal::HorizonGpuUpdate;
using terrain::unreal::kNormalHalves;
using terrain::unreal::kNormalPendingHalf;
using terrain::unreal::NormalRegion;

void testHalves() {
    TEST_CHECK(halfFromFloat(0.0f) == 0 && halfFromFloat(-0.0f) == 0x8000);
    TEST_CHECK(halfFromFloat(1.0f) == 0x3C00 && halfFromFloat(-2.0f) == 0xC000);
    TEST_CHECK(halfFromFloat(0.1f) == 0x2E66 && halfFromFloat(65504.0f) == 0x7BFF);
    TEST_CHECK(halfFromFloat(70000.0f) == 0x7C00 && halfFromFloat(1e-9f) == 0);
    TEST_CHECK(halfFromFloat(std::numeric_limits<float>::infinity()) == 0x7C00);
    TEST_CHECK((halfFromFloat(std::numeric_limits<float>::quiet_NaN()) & 0x7FFF) > 0x7C00);
    // Ties to even: 1 + 2^-11 lies between 1 and its next half, 1 + 3 * 2^-11 above the next.
    TEST_CHECK(halfFromFloat(1.0f + 0x1p-11f) == 0x3C00);
    TEST_CHECK(halfFromFloat(1.0f + 3 * 0x1p-11f) == 0x3C02);
    TEST_CHECK(halfFromFloat(0x1p-24f) == 1 && halfFromFloat(0x1p-25f) == 0); // subnormals
    bool roundTrip = true;
    for (std::uint32_t half = 0; half < 0x10000U; ++half) {
        const auto h = static_cast<std::uint16_t>(half);
        if ((h & 0x7C00) == 0x7C00 && (h & 0x3FF) != 0) {
            continue; // NaN
        }
        roundTrip = roundTrip && halfFromFloat(floatFromHalf(h)) == h;
    }
    TEST_CHECK(roundTrip);
    TEST_CHECK(floatFromHalf(kNormalPendingHalf) > terrain::unreal::kNormalPendingTest);
    const std::vector<terrain::DetailSlope> slopes{{5000.0f, -0.5f}, {-1e9f, 2.0f}};
    const std::vector<std::uint16_t> texels = terrain::unreal::normalTexels(slopes);
    TEST_CHECK(texels.size() == 4 && floatFromHalf(texels[0]) == 1000.0f &&
               floatFromHalf(texels[1]) == -0.5f && floatFromHalf(texels[2]) == -1000.0f);
}

// A rectangle of 3 x 2 samples across the wrap of the slots (side 8) at ratio 2: its texel
// (w, v) holds the halves w and 1000 + v.
void testRegions() {
    const HorizonRect rect{6, 15, 9, 17}; // slots 6, 7, 0 and 7, 0
    std::vector<std::uint16_t> data;
    for (int v = 0; v < 4; ++v) {
        for (int w = 0; w < 6; ++w) {
            data.push_back(static_cast<std::uint16_t>(w));
            data.push_back(static_cast<std::uint16_t>(1000 + v));
        }
    }
    const std::vector<NormalRegion> regions = terrain::unreal::normalRegions(1, 8, 2, rect, data);
    TEST_CHECK(regions.size() == 4);
    bool right = true;
    for (const NormalRegion& region : regions) {
        right = right && region.level == 1;
        for (int row = 0; row < region.height; ++row) {
            for (int column = 0; column < region.width; ++column) {
                // Back from the texel of the texture to the texel of the rectangle.
                const int w = (region.x + column - 12 + 16) % 16;
                const int v = (region.z + row - 14 + 16) % 16;
                const auto at = static_cast<std::size_t>((row * region.width + column) * 2);
                right = right && region.data[at] == w && region.data[at + 1] == 1000 + v;
            }
        }
    }
    TEST_CHECK(right);
    TEST_CHECK(regions[0].x == 12 && regions[0].z == 14 && regions[0].width == 4 &&
               regions[0].height == 2);
    const std::vector<std::uint16_t> part =
        terrain::unreal::partTexels(rect, 2, data, HorizonRect{7, 16, 9, 17});
    TEST_CHECK(part.size() == 16 && part[0] == 2 && part[1] == 1002 && part[14] == 5 &&
               part[15] == 1003);
    const std::vector<NormalRegion> pending = terrain::unreal::pendingRegions(0, 8, 2, rect);
    TEST_CHECK(pending.size() == 4 && pending[3].data.front() == kNormalPendingHalf);
    data.pop_back();
    bool thrown = false;
    try {
        (void)terrain::unreal::normalRegions(1, 8, 2, rect, data);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    TEST_CHECK(thrown);
}

// Jobs run when the test says so.
struct Jobs {
    std::deque<std::function<void()>> queue;
    void runAll() {
        while (!queue.empty()) {
            std::function<void()> job = std::move(queue.front());
            queue.pop_front();
            job();
        }
    }
};

constexpr int kRatio = 2;

HorizonGpuParams smallParams() {
    HorizonGpuParams params;
    params.levels = 4;
    params.cellsPerSide = 128;
    params.lod = {{1, 3, 7}, 23};
    params.normalRatio = kRatio;
    return params;
}

// The detail textures the GPU holds and where each level stands.
class Gpu {
public:
    void apply(const HorizonGpuUpdate& update, int side) {
        const int texSide = side * kRatio;
        for (const NormalRegion& region : update.normalRegions) {
            std::vector<std::uint16_t>& texture = textures_[region.level];
            // Created pending, as the textures of UTerrainHorizonClipmap.
            texture.resize(static_cast<std::size_t>(texSide * texSide * kNormalHalves),
                           kNormalPendingHalf);
            for (int row = 0; row < region.height; ++row) {
                for (int h = 0; h < region.width * kNormalHalves; ++h) {
                    texture[static_cast<std::size_t>(((region.z + row) * texSide + region.x) *
                                                         kNormalHalves + h)] =
                        region.data[static_cast<std::size_t>(row * region.width * kNormalHalves + h)];
                }
            }
        }
        for (const terrain::unreal::LevelPlacement& placement : update.placements) {
            first_[placement.level] = placement.firstSlot;
        }
    }

    // Texels of the window of a level not pending, and equal to the detail of the sampler.
    [[nodiscard]] int pending(int level, int side) const { return count(level, side, nullptr); }
    [[nodiscard]] int wrong(int level, int side, const std::vector<std::uint16_t>& expected) const {
        return count(level, side, &expected);
    }

private:
    int count(int level, int side, const std::vector<std::uint16_t>* expected) const {
        const terrain::HorizonIndex first = first_.at(level);
        const int texSide = side * kRatio;
        const auto found_ = textures_.find(level); // none uploaded yet: every texel pending
        const std::vector<std::uint16_t> created(
            static_cast<std::size_t>(texSide * texSide * kNormalHalves), kNormalPendingHalf);
        const std::vector<std::uint16_t>& texture =
            found_ != textures_.end() ? found_->second : created;
        int found = 0;
        for (int v = 0; v < side * kRatio; ++v) {
            for (int w = 0; w < side * kRatio; ++w) {
                const auto x = static_cast<int>(((first.x + w / kRatio) % side) * kRatio + w % kRatio);
                const auto z = static_cast<int>(((first.z + v / kRatio) % side) * kRatio + v % kRatio);
                const auto at = static_cast<std::size_t>((z * texSide + x) * kNormalHalves);
                if (expected == nullptr) {
                    found += texture[at] == kNormalPendingHalf ? 1 : 0;
                } else {
                    const auto e = static_cast<std::size_t>((v * texSide + w) * kNormalHalves);
                    found += texture[at] != (*expected)[e] || texture[at + 1] != (*expected)[e + 1];
                }
            }
        }
        return found;
    }

    std::map<int, std::vector<std::uint16_t>> textures_;
    std::map<int, terrain::HorizonIndex> first_;
};

int wrongTexels(const Gpu& gpu, const HorizonGpuStreamer& streamer,
                const terrain::HorizonNormalSampler& sampler) {
    int wrong = 0;
    for (int level = 0; level < streamer.params().levels; ++level) {
        const HorizonRect window = streamer.clipmap().level(level).window();
        wrong += gpu.wrong(level, streamer.side(),
                           terrain::unreal::normalTexels(sampler.sample(level, window)));
    }
    return wrong;
}

void settle(HorizonGpuStreamer& streamer, Gpu& gpu, Jobs& jobs, double x, double z) {
    const terrain::unreal::UnrealFrame frame{{horizon_test::kX, 0.0, horizon_test::kZ}};
    const terrain::unreal::UnrealPoint player{(x - horizon_test::kX) * 100.0,
                                              (z - horizon_test::kZ) * 100.0, 0.0};
    for (int n = 0; n < 50; ++n) {
        gpu.apply(streamer.update(player, frame), streamer.side());
        jobs.runAll();
        if (!streamer.normals()->busy() && n > 3) {
            break;
        }
    }
}

void testStream() {
    const horizon_test::World world;
    Jobs jobs;
    HorizonGpuStreamer streamer(world.field, world.ecology, terrain::WorldSize{}, smallParams(),
                                [&](std::function<void()> job) { jobs.queue.push_back(std::move(job)); });
    const terrain::HorizonNormalSampler sampler(world.field, streamer.params(), kRatio);
    const terrain::unreal::UnrealFrame frame{{horizon_test::kX, 0.0, horizon_test::kZ}};
    Gpu gpu;
    // The first epoch: its samples computed by the jobs, then committed with every detail pending.
    gpu.apply(streamer.update({}, frame), streamer.side());
    while (!streamer.shown()) {
        jobs.runAll();
        gpu.apply(streamer.update({}, frame), streamer.side());
    }
    const int texels = streamer.side() * kRatio * streamer.side() * kRatio;
    TEST_CHECK(gpu.pending(0, streamer.side()) == texels);
    TEST_CHECK(gpu.pending(3, streamer.side()) == texels);
    settle(streamer, gpu, jobs, horizon_test::kX, horizon_test::kZ);
    TEST_CHECK(gpu.pending(0, streamer.side()) == 0 && wrongTexels(gpu, streamer, sampler) == 0);
    // A walk of 100 m, settled; another of 100 m, committed but its detail not computed, then a
    // jump of 30 km: the detail of the second walk is computed after the jump moved its samples
    // out of the windows, and dropped.
    settle(streamer, gpu, jobs, horizon_test::kX + 100.0, horizon_test::kZ - 40.0);
    TEST_CHECK(wrongTexels(gpu, streamer, sampler) == 0);
    const auto moveTo = [&](const terrain::unreal::UnrealPoint& player) {
        const terrain::HorizonRect before = streamer.clipmap().level(0).window();
        for (int n = 0; n < 10'000 && streamer.clipmap().level(0).window() == before; ++n) {
            gpu.apply(streamer.update(player, frame), streamer.side());
            if (streamer.clipmap().level(0).window() == before && !jobs.queue.empty()) {
                jobs.queue.front()(); // one job at a time, in their order
                jobs.queue.pop_front();
            }
        }
    };
    moveTo({20'000.0, -4'000.0, 0.0});
    const std::size_t queued = jobs.queue.size();
    moveTo({3'000'000.0, 0.0, 0.0});
    TEST_CHECK(queued > 0 && gpu.pending(0, streamer.side()) > 0); // the new window, pending
    settle(streamer, gpu, jobs, horizon_test::kX + 30'000.0, horizon_test::kZ);
    TEST_CHECK(wrongTexels(gpu, streamer, sampler) == 0);
    TEST_CHECK(streamer.normals()->samplesComputed() > 0);
    TEST_CHECK(streamer.normals()->textureBytes() ==
               4LL * streamer.side() * kRatio * streamer.side() * kRatio * 4);
}

void testWithout() {
    const horizon_test::World world;
    HorizonGpuParams params = smallParams();
    params.normalRatio = 0;
    HorizonGpuStreamer streamer(world.field, world.ecology, terrain::WorldSize{}, params,
                                [](std::function<void()> job) { job(); });
    TEST_CHECK(streamer.normals() == nullptr);
    const HorizonGpuUpdate update = streamer.update({}, {{horizon_test::kX, 0.0, horizon_test::kZ}});
    TEST_CHECK(!update.regions.empty() && update.normalRegions.empty());
    params.normalRatio = 3;
    bool thrown = false;
    try {
        HorizonGpuStreamer refused(world.field, world.ecology, terrain::WorldSize{}, params,
                                   [](std::function<void()> job) { job(); });
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    TEST_CHECK(thrown);
}

} // namespace

int main() {
    testHalves();
    testRegions();
    try {
        testStream();
        testWithout();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "exception: %s\n", e.what());
        TEST_CHECK(false);
    }
    return test::result();
}

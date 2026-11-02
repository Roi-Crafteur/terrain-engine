// The surface blend of the GPU horizon of the Unreal plugin (agent 29,
// Bridge/horizon_blend_texels):
// - the 4 channels of 16 bits of a blend (ids, weights, the present bit) and back, the zeros of a
//   new texture read as no blend, the regions split where the slots wrap around, a channel that
//   is not an integer of 16 bits refused;
// - the blends written in the same update as the texels of their samples: while a new place is
//   brought level by level (agent 38) and after a teleport, every sample of a level shown has its
//   blend, the one of the clipmap, at every update (the material never shows a sample without
//   its blend, so its color never changes when the blend arrives);
// - the blend of every sample of every window once settled, as a sampler computes it.
#include "horizon_blend_texels.hpp"
#include "horizon_gpu_streamer.hpp"

#include "horizon_test_support.hpp"
#include "test_assert.hpp"
#include "unreal_horizon_gpu_support.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

using terrain::BlendTexel;
using terrain::HorizonRect;
using terrain::unreal::HorizonGpuParams;
using terrain::unreal::HorizonGpuStreamer;
using terrain::unreal::HorizonGpuUpdate;
using terrain::unreal::kBlendChannels;
using terrain::unreal::UnrealFrame;
using terrain::unreal::UnrealPoint;

void testChannels() {
    const BlendTexel texel{0x00ABCDEFU, 0x12345678U};
    const auto channels = terrain::unreal::blendChannels(texel);
    TEST_CHECK(channels ==
               (std::array<std::uint16_t, kBlendChannels>{0xCDEF, 0x80AB, 0x5678, 0x1234}));
    TEST_CHECK(terrain::unreal::blendOfChannels(channels) == texel);
    // The zeros of a new texture, and bits blendChannels never sets.
    TEST_CHECK(!terrain::unreal::blendOfChannels({0, 0, 0, 0}).has_value());
    TEST_CHECK(!terrain::unreal::blendOfChannels({0, 0x81AB, 0, 0}).has_value());
    // A blend of zeros is a blend (the ocean, all on slot 2).
    TEST_CHECK(terrain::unreal::blendOfChannels(terrain::unreal::blendChannels({})) ==
               BlendTexel{});
}

void testRegions() {
    constexpr int kSide = 8;
    const HorizonRect rect{6, -1, 10, 1}; // wraps along x (slots 6, 7 | 0, 1) and z (7 | 0)
    std::vector<BlendTexel> blends;
    for (std::uint32_t n = 0; n < static_cast<std::uint32_t>(rect.count()); ++n) {
        blends.push_back({n, 0x01000000U * n + 7U});
    }
    std::vector<float> data = {1.5f}; // what comes before the blends is left as it is
    terrain::unreal::appendBlendUploads(blends, data);
    TEST_CHECK(data.size() == 1 + blends.size() * kBlendChannels && data[0] == 1.5f);
    const std::span<const float> channels = std::span<const float>(data).subspan(1);
    const std::vector<terrain::unreal::BlendRegion> regions =
        terrain::unreal::blendRegions(3, kSide, rect, channels);
    TEST_CHECK(regions.size() == 4);
    bool placed = true;
    for (const terrain::unreal::BlendRegion& region : regions) {
        TEST_CHECK(region.level == 3);
        for (int k = 0; k < region.height; ++k) {
            for (int i = 0; i < region.width; ++i) {
                const auto at = static_cast<std::size_t>((k * region.width + i) * kBlendChannels);
                std::array<std::uint16_t, kBlendChannels> texel{};
                std::copy_n(region.data.begin() + static_cast<std::ptrdiff_t>(at), kBlendChannels,
                            texel.begin());
                // The sample of this slot.
                const std::int64_t sx = region.x + i >= 6 ? region.x + i : region.x + i + kSide;
                const std::int64_t sz = region.z + k == 7 ? -1 : region.z + k;
                const auto n =
                    static_cast<std::size_t>((sz - rect.z0) * rect.width() + sx - rect.x0);
                placed = placed && terrain::unreal::blendOfChannels(texel) == blends[n];
            }
        }
    }
    TEST_CHECK(placed);
    data.back() = 0.5f;
    bool refused = false;
    try {
        (void)terrain::unreal::blendRegions(3, kSide, rect,
                                            std::span<const float>(data).subspan(1));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    TEST_CHECK(refused);
}

const UnrealFrame kFrame{{horizon_test::kX, 1234.5, horizon_test::kZ}};

HorizonGpuParams progressiveParams() {
    HorizonGpuParams result;
    result.levels = 5;
    result.cellsPerSide = 128;
    result.lod = {{1, 3, 7}, 23};
    result.normalRatio = 0;
    result.crestRatio = 0;
    result.progressive = true;
    return result;
}

// Every sample of every level shown has its blend, the one of the clipmap.
bool shownHaveBlends(const gpu_test::Gpu& gpu, const terrain::HorizonClipmap& clipmap) {
    for (const auto& [level, placement] : gpu.placements()) {
        if (!placement.visible) {
            continue;
        }
        const terrain::HorizonLevel& samples = clipmap.level(level);
        for (int k = 0; k < samples.side(); ++k) {
            for (int i = 0; i < samples.side(); ++i) {
                if (gpu.blend(level, i, k) != samples.atWindow(i, k).blend) {
                    return false;
                }
            }
        }
    }
    return true;
}

// The start level by level, then a teleport of 300 km in the middle of a step.
void testLevelByLevelAndTeleport() {
    const horizon_test::World world;
    gpu_test::Jobs jobs;
    HorizonGpuStreamer streamer(world.field, world.ecology, terrain::WorldSize{},
                                progressiveParams(), jobs.launcher());
    gpu_test::Gpu gpu(129);
    int updates = 0;
    int shown = 0;
    const auto settle = [&](double x, double z) {
        for (int round = 0; round < 100; ++round) {
            gpu.apply(streamer.update(terrain::unreal::toUnreal(kFrame, {x, 0.0, z}), kFrame));
            ++updates;
            TEST_CHECK(shownHaveBlends(gpu, streamer.clipmap()));
            shown = std::max(shown, streamer.levelsShown());
            if (!streamer.busy()) {
                return;
            }
            jobs.runAll();
        }
    };
    settle(horizon_test::kX, horizon_test::kZ);
    TEST_CHECK(shown == 5);
    gpu.apply(streamer.update(
        terrain::unreal::toUnreal(kFrame, {horizon_test::kX + 300.0, 0.0, horizon_test::kZ}),
        kFrame));
    TEST_CHECK(shownHaveBlends(gpu, streamer.clipmap()));
    settle(horizon_test::kX + 300'000.0, horizon_test::kZ);
    TEST_CHECK(streamer.levelsShown() == 5);
    const terrain::HorizonSampler sampler(world.field, world.ecology, streamer.params());
    TEST_CHECK(gpu_test::texelsMatch(gpu, streamer.clipmap(), sampler));
    std::printf("level by level and after a teleport: %d updates, every sample shown with its "
                "blend\n",
                updates);
}

} // namespace

int main() {
    testChannels();
    testRegions();
    testLevelByLevelAndTeleport();
    return test::result();
}

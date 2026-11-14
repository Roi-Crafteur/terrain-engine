#include "horizon_blend_texels.hpp"

#include "horizon_textures.hpp"

#include <cstddef>
#include <stdexcept>
#include <utility>

namespace terrain::unreal {

namespace {

// Every channel is an integer below 2^24 (biome_blend.hpp: kBlendFieldBits).
constexpr float kFieldLimit = static_cast<float>(1u << kBlendFieldBits);

} // namespace

std::array<float, kBlendChannels> blendChannels(BlendTexel texel) {
    return blendTexelFloats(texel);
}

std::optional<BlendTexel> blendOfChannels(const std::array<float, kBlendChannels>& channels) {
    return blendTexelOf(channels);
}

void appendBlendUploads(std::span<const BlendTexel> blends, std::vector<float>& data) {
    data.reserve(data.size() + blends.size() * kBlendChannels);
    for (const BlendTexel blend : blends) {
        for (const float channel : blendChannels(blend)) {
            data.push_back(channel);
        }
    }
}

std::vector<BlendRegion> blendRegions(int level, int side, const HorizonRect& rect,
                                      std::span<const float> channels) {
    if (rect.width() > side || rect.height() > side ||
        channels.size() != static_cast<std::size_t>(rect.count()) * kBlendChannels) {
        throw std::invalid_argument("blendRegions: the channels do not fit the rectangle");
    }
    std::vector<BlendRegion> regions;
    for (const SlotSpan& z : slotSpans(rect.z0, rect.z1, side)) {
        for (const SlotSpan& x : slotSpans(rect.x0, rect.x1, side)) {
            BlendRegion region{level, x.slot, z.slot, x.length, z.length, {}};
            region.data.reserve(static_cast<std::size_t>(x.length) *
                                static_cast<std::size_t>(z.length) * kBlendChannels);
            for (int row = 0; row < z.length; ++row) {
                const std::int64_t first =
                    ((z.sample - rect.z0 + row) * rect.width() + (x.sample - rect.x0)) *
                    kBlendChannels;
                for (int n = 0; n < x.length * kBlendChannels; ++n) {
                    const float value = channels[static_cast<std::size_t>(first + n)];
                    if (!(value >= 0.0f && value < kFieldLimit) ||
                        static_cast<float>(static_cast<std::uint32_t>(value)) != value) {
                        throw std::invalid_argument("blendRegions: a channel is not an integer "
                                                    "below 2^24");
                    }
                    region.data.push_back(value);
                }
            }
            regions.push_back(std::move(region));
        }
    }
    return regions;
}

} // namespace terrain::unreal

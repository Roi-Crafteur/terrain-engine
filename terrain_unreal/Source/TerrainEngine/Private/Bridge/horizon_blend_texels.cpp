#include "horizon_blend_texels.hpp"

#include "horizon_textures.hpp"

#include <cstddef>
#include <stdexcept>
#include <utility>

namespace terrain::unreal {

std::array<std::uint16_t, kBlendChannels> blendChannels(BlendTexel texel) {
    return {static_cast<std::uint16_t>(texel.ids & 0xFFFFU),
            static_cast<std::uint16_t>(((texel.ids >> 16U) & 0xFFU) |
                                       ((texel.ids >> 24U) & 0x7FU) << 8U | kBlendTexelPresent),
            static_cast<std::uint16_t>(texel.weights & 0xFFFFU),
            static_cast<std::uint16_t>(texel.weights >> 16U)};
}

std::optional<BlendTexel>
blendOfChannels(const std::array<std::uint16_t, kBlendChannels>& channels) {
    const std::uint32_t high = channels[1];
    if ((high & kBlendTexelPresent) == 0) {
        return std::nullopt;
    }
    return BlendTexel{channels[0] | (high & 0xFFU) << 16U | ((high >> 8U) & 0x7FU) << 24U,
                      channels[2] | static_cast<std::uint32_t>(channels[3]) << 16U};
}

void appendBlendUploads(std::span<const BlendTexel> blends, std::vector<float>& data) {
    data.reserve(data.size() + blends.size() * kBlendChannels);
    for (const BlendTexel blend : blends) {
        for (const std::uint16_t channel : blendChannels(blend)) {
            data.push_back(static_cast<float>(channel));
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
                    if (!(value >= 0.0f && value <= 65535.0f) ||
                        static_cast<float>(static_cast<std::uint16_t>(value)) != value) {
                        throw std::invalid_argument("blendRegions: a channel is not an integer "
                                                    "of 16 bits");
                    }
                    region.data.push_back(static_cast<std::uint16_t>(value));
                }
            }
            regions.push_back(std::move(region));
        }
    }
    return regions;
}

} // namespace terrain::unreal

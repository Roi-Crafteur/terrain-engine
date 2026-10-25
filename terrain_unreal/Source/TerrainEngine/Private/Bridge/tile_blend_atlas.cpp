#include "tile_blend_atlas.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <stdexcept>

namespace terrain::unreal {

namespace {

constexpr std::uint32_t kIdMask = 0xFFFFFFu; // the ids fill bits 0-23 (biome_blend.hpp)
constexpr std::uint32_t kHalfMask = 0xFFFFu;
constexpr int kMaxSlotsPerSide = 1024;

// The integer a float holds when it is one in [0, limit].
std::optional<std::uint32_t> wholeUpTo(float value, std::uint32_t limit) {
    if (!(value >= 0.0f) || value > static_cast<float>(limit)) {
        return std::nullopt;
    }
    const auto whole = static_cast<std::uint32_t>(value);
    if (static_cast<float>(whole) != value) {
        return std::nullopt;
    }
    return whole;
}

} // namespace

std::array<float, kBlendTexelFloats> blendTexelFloats(BlendTexel texel) {
    // Integers below 2^24 are exact in a float.
    return {static_cast<float>(texel.ids & kIdMask), static_cast<float>(texel.weights & kHalfMask),
            static_cast<float>(texel.weights >> 16u), 1.0f};
}

std::optional<BlendTexel> blendTexelOf(const std::array<float, kBlendTexelFloats>& floats) {
    if (floats[3] != 1.0f) {
        return std::nullopt;
    }
    const std::optional<std::uint32_t> ids = wholeUpTo(floats[0], kIdMask);
    const std::optional<std::uint32_t> low = wholeUpTo(floats[1], kHalfMask);
    const std::optional<std::uint32_t> high = wholeUpTo(floats[2], kHalfMask);
    if (!ids || !low || !high) {
        return std::nullopt;
    }
    return BlendTexel{*ids, *low | (*high << 16u)};
}

std::vector<float> blendRegion(const std::vector<BlendTexel>& texels) {
    if (texels.size() != static_cast<std::size_t>(kTileBlendTexels * kTileBlendTexels)) {
        throw std::invalid_argument("blendRegion: a tile holds kTileBlendTexels^2 texels");
    }
    std::vector<float> region;
    region.reserve(texels.size() * kBlendTexelFloats);
    for (const BlendTexel& texel : texels) {
        const std::array<float, kBlendTexelFloats> floats = blendTexelFloats(texel);
        region.insert(region.end(), floats.begin(), floats.end());
    }
    return region;
}

BlendAtlas::BlendAtlas(int slotsPerSide) : slotsPerSide_(slotsPerSide) {
    if (slotsPerSide < 1 || slotsPerSide > kMaxSlotsPerSide) {
        throw std::invalid_argument("BlendAtlas: slotsPerSide must be in [1, 1024]");
    }
}

std::optional<int> BlendAtlas::place(const TileKey& key) {
    if (const auto it = slots_.find(key); it != slots_.end()) {
        return it->second;
    }
    int slot = 0;
    if (!free_.empty()) {
        std::pop_heap(free_.begin(), free_.end(), std::greater<>{});
        slot = free_.back();
        free_.pop_back();
    } else if (next_ < capacity()) {
        slot = next_++;
    } else {
        return std::nullopt;
    }
    slots_.emplace(key, slot);
    return slot;
}

void BlendAtlas::release(const TileKey& key) {
    const auto it = slots_.find(key);
    if (it == slots_.end()) {
        return;
    }
    free_.push_back(it->second);
    std::push_heap(free_.begin(), free_.end(), std::greater<>{});
    slots_.erase(it);
}

void BlendAtlas::clear() {
    slots_.clear();
    free_.clear();
    next_ = 0;
}

std::optional<int> BlendAtlas::slotOf(const TileKey& key) const {
    const auto it = slots_.find(key);
    if (it == slots_.end()) {
        return std::nullopt;
    }
    return it->second;
}

BlendAtlas::Corner BlendAtlas::corner(int slot) const {
    if (slot < 0 || slot >= capacity()) {
        throw std::out_of_range("BlendAtlas::corner: no such slot");
    }
    return {(slot % slotsPerSide_) * kTileBlendTexels, (slot / slotsPerSide_) * kTileBlendTexels};
}

} // namespace terrain::unreal

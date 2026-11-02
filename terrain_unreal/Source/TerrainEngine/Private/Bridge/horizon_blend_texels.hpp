#pragma once

#include "terrain/generation/biome_blend.hpp"
#include "terrain/streaming/horizon_levels.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace terrain::unreal {

// The surface blend of the GPU horizon (agent 29): a third texture per level, at the slots of
// the texels, holding the blend of each sample (HorizonCell::blend: the weights of its 2 or 3
// dominant biomes and materials, as the texels of the tiles). A texel is 4 channels of 16 bits
// (PF_A16B16G16R16 in Unreal: RGBA, unsigned normalized, 8 bytes), R first:
// - R: bits 0-15 of the ids, G: bits 16-23 of the ids plus kBlendTexelPresent (bit 15);
// - B: bits 0-15 of the weights, A: bits 16-31 of the weights.
// The material reads them without filtering and decodes each channel as round(65535 x), the
// integer it holds: a texel whose G lacks kBlendTexelPresent holds no blend yet (the zeros of a
// texture just created), and the material then takes the packed surface of the texel
// (horizon_textures.hpp) as before the blend.
inline constexpr int kBlendChannels = 4;
inline constexpr std::uint16_t kBlendTexelPresent = 0x8000U;

[[nodiscard]] std::array<std::uint16_t, kBlendChannels> blendChannels(BlendTexel texel);
// The blend of 4 channels written by blendChannels; nothing without kBlendTexelPresent or with
// bits blendChannels never sets.
[[nodiscard]] std::optional<BlendTexel>
blendOfChannels(const std::array<std::uint16_t, kBlendChannels>& channels);

// The blend channels of the cells of a computed piece, in the order of its cells, after the
// texels and the water levels of pieceUploads (horizon_textures.hpp): kBlendChannels floats per
// cell, each an integer of 16 bits (exact in a float).
void appendBlendUploads(std::span<const BlendTexel> blends, std::vector<float>& data);

// Blend texels to write into the blend texture of a level, slots [x, x + width) x
// [z, z + height), kBlendChannels channels per texel, row by row.
struct BlendRegion {
    int level = 0;
    int x = 0;
    int z = 0;
    int width = 0;
    int height = 0;
    std::vector<std::uint16_t> data;
};

// The regions of the blend texels of a rectangle of samples of a level, given as
// appendBlendUploads lays them out: split as texelRegions where the slots wrap around. Throws
// std::invalid_argument for data of another size, a channel that is not an integer of 16 bits,
// or a rectangle wider than the texture.
[[nodiscard]] std::vector<BlendRegion> blendRegions(int level, int side, const HorizonRect& rect,
                                                    std::span<const float> channels);

} // namespace terrain::unreal

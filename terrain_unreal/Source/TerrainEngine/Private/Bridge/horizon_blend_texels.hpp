#pragma once

#include "tile_blend_atlas.hpp"

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
// dominant biomes and materials, as the texels of the tiles). A texel is 4 channels of 32-bit
// floats (PF_A32B32G32R32F in Unreal, 16 bytes), the same as a texel of the atlas of the tiles
// (tile_blend_atlas.hpp: blendTexelFloats): the 4 fields of the blend texel (biome_blend.hpp), each
// an exact integer below 2^24, kBlendPresentBit set in the first. The material reads them without
// filtering: a texel without kBlendPresentBit holds no blend yet (the zeros of a texture just
// created), and the material then takes the packed surface of the texel (horizon_textures.hpp)
// as before the blend.
inline constexpr int kBlendChannels = kBlendTexelFloats;

[[nodiscard]] std::array<float, kBlendChannels> blendChannels(BlendTexel texel);
// The blend of 4 channels written by blendChannels; nothing without kBlendPresentBit.
[[nodiscard]] std::optional<BlendTexel>
blendOfChannels(const std::array<float, kBlendChannels>& channels);

// The blend channels of the cells of a computed piece, in the order of its cells, after the
// texels and the water levels of pieceUploads (horizon_textures.hpp): kBlendChannels floats per
// cell.
void appendBlendUploads(std::span<const BlendTexel> blends, std::vector<float>& data);

// Blend texels to write into the blend texture of a level, slots [x, x + width) x
// [z, z + height), kBlendChannels channels per texel, row by row.
struct BlendRegion {
    int level = 0;
    int x = 0;
    int z = 0;
    int width = 0;
    int height = 0;
    std::vector<float> data;
};

// The regions of the blend texels of a rectangle of samples of a level, given as
// appendBlendUploads lays them out: split as texelRegions where the slots wrap around. Throws
// std::invalid_argument for data of another size, a channel that is not an integer below 2^24,
// or a rectangle wider than the texture.
[[nodiscard]] std::vector<BlendRegion> blendRegions(int level, int side, const HorizonRect& rect,
                                                    std::span<const float> channels);

} // namespace terrain::unreal

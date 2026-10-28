#pragma once

#include "terrain/generation/biome_blend.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/stream_manager.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

namespace terrain::unreal {

// The surface blend of the tiles (TileData::surfaceBlend: the weights of the 2 or 3 dominant
// biomes and materials, every 4 cells of a tile) for the materials of Unreal. Every shown tile
// takes a slot of one texture of float texels (the atlas), kTileBlendTexels x kTileBlendTexels
// texels from its lowest corner, and tells the material its slot and the meters between its
// texels; the vertex shader of the temporary terrain material reads the 4 texels around each
// vertex and adds the weight of each slot to its id (biome_blend.hpp: addTexel), never
// interpolating the ids (Scripts/temporary_terrain_hlsl.py).

// Floats of one texel of the atlas (an RGBA texel of 32-bit floats).
inline constexpr int kBlendTexelFloats = 4;

// A texel of the atlas, exact in floats: the ids (24 bits), the low and the high 16 bits of the
// weights, and 1 (a texel of a tile: the texels of no tile hold 0).
[[nodiscard]] std::array<float, kBlendTexelFloats> blendTexelFloats(BlendTexel texel);
// The texel of floats written by blendTexelFloats; nothing unless the last float is 1 and the
// others hold integers of their range.
[[nodiscard]] std::optional<BlendTexel> blendTexelOf(const std::array<float, kBlendTexelFloats>& floats);

// The texels of a tile as a region of the atlas, row after row (the rows of the texels along z),
// kBlendTexelFloats floats per texel. Throws std::invalid_argument unless there are
// kTileBlendTexels x kTileBlendTexels texels.
[[nodiscard]] std::vector<float> blendRegion(const std::vector<BlendTexel>& texels);

// Meters between the texels of a tile of this LOD (4, 8, 16, 32 m on LOD 0 to 3).
[[nodiscard]] constexpr double blendPitchMeters(int lod) {
    return kTileBlendStride * tileCellMeters(lod);
}

// The slots of the atlas, slotsPerSide x slotsPerSide, given to the shown tiles. A tile keeps
// its slot until it is released; released slots are given again, lowest first.
class BlendAtlas {
public:
    // Throws std::invalid_argument unless slotsPerSide is in [1, 1024].
    explicit BlendAtlas(int slotsPerSide);

    // The slot of the tile: the one it holds, or a free one; nothing when every slot is taken.
    [[nodiscard]] std::optional<int> place(const TileKey& key);
    // Frees the slot of the tile; nothing for a tile without one.
    void release(const TileKey& key);
    void clear();

    // The slot a tile holds, if any.
    [[nodiscard]] std::optional<int> slotOf(const TileKey& key) const;
    [[nodiscard]] std::size_t size() const { return slots_.size(); }
    [[nodiscard]] int capacity() const { return slotsPerSide_ * slotsPerSide_; }
    [[nodiscard]] int slotsPerSide() const { return slotsPerSide_; }
    // Texels along each side of the atlas texture.
    [[nodiscard]] int textureSide() const { return slotsPerSide_ * kTileBlendTexels; }

    // The texel of the atlas at the lowest corner of a slot: column x, row y.
    struct Corner {
        int x = 0;
        int y = 0;

        friend bool operator==(const Corner&, const Corner&) = default;
    };
    [[nodiscard]] Corner corner(int slot) const;

private:
    int slotsPerSide_;
    int next_ = 0;          // the slots from next_ on were never given
    std::vector<int> free_; // released slots, a min-heap
    std::unordered_map<TileKey, int, TileKeyHash> slots_;
};

} // namespace terrain::unreal

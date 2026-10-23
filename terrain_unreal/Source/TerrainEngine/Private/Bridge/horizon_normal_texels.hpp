#pragma once

#include "terrain/streaming/horizon_levels.hpp"
#include "terrain/streaming/horizon_normals.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace terrain::unreal {

// The detail normals of the GPU horizon: per level, a texture of (K + 1) * ratio texels per side,
// two half floats each (PF_G16R16F: the slopes along x and z of a DetailSlope), sampled
// bilinearly by the materials. Texel (a, b) of sample s lies at slotOf(s) * ratio + (a, b): the
// texels of a sample move with it, as the texels of the samples (horizon_textures.hpp).
//
// The detail normals are computed after the samples (HorizonNormalStream), so a texel may not be
// ready when its level is shown: it then holds kNormalPendingHalf on both channels, which no
// slope reaches (they are clamped to kNormalSlopeLimit). A material reading a slope beyond
// kNormalPendingTest (a pending texel, or a texel filtered with one) falls back to the normal of
// the grid.
inline constexpr int kNormalHalves = 2;
inline constexpr float kNormalSlopeLimit = 1000.0f;   // 89.94 degrees
inline constexpr std::uint16_t kNormalPendingHalf = 0x7BFF; // 65504, the largest half float
inline constexpr float kNormalPendingTest = 2000.0f;

// The half float nearest to a float (ties to even), infinities and NaN kept.
[[nodiscard]] std::uint16_t halfFromFloat(float value);
[[nodiscard]] float floatFromHalf(std::uint16_t half);

// The texels of detail slopes as half floats, two per slope, clamped to kNormalSlopeLimit.
[[nodiscard]] std::vector<std::uint16_t> normalTexels(std::span<const DetailSlope> slopes);

// Texels to write into the detail normal texture of a level: texels [x, x + width) x
// [z, z + height), their halves row by row.
struct NormalRegion {
    int level = 0;
    int x = 0;
    int z = 0;
    int width = 0;
    int height = 0;
    std::vector<std::uint16_t> data;
};

// The regions of the detail texels of a rectangle of samples of a level, laid out as
// HorizonNormalSampler::fill lays them out (normalTexels): at most 4, split where the slots of
// the samples wrap around (side samples per side of the window). Throws std::invalid_argument
// for data of another size or a rectangle wider than the window.
[[nodiscard]] std::vector<NormalRegion> normalRegions(int level, int side, int ratio,
                                                      const HorizonRect& rect,
                                                      std::span<const std::uint16_t> data);

// The same regions, every texel pending.
[[nodiscard]] std::vector<NormalRegion> pendingRegions(int level, int side, int ratio,
                                                       const HorizonRect& rect);

} // namespace terrain::unreal

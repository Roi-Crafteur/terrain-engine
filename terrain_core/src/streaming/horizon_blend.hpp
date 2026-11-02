#pragma once

// The surface blend of the samples of the far horizon (HorizonCell::blend), internal to
// terrain_core: for horizon_sampler.cpp.

#include "terrain/generation/biome_blend.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/terrain_surface.hpp"

#include <array>

namespace terrain::horizon {

// Site of the ground at a point of the relief (its sample) from the heights of its 4 neighbors
// step meters away (east, west, south, north), with the water of the field there.
//
// A copy of the site of surfaceFromHeightfield (setSlopes and setWater in
// generation/terrain_surface.cpp): the same expressions, so that the same inputs give the same
// bits. horizon_sampler_test (testMaterialOracle) compares the materials and biomes of the
// horizon with Ecology::map(surfaceFromHeightfield(...)) bit for bit in several regions: it
// fails if the two ever diverge.
[[nodiscard]] SurfaceSite pointSite(const HeightSample& sample, const std::array<float, 4>& h,
                                    double step);

// The dominant biomes and materials of summed weights: the kBlendSlots heaviest of each (the
// lower id first between equal weights), their weights divided by their sum. A slot of weight 0
// repeats the id of slot 0, as blendSurface leaves it. The weights must not all be 0.
[[nodiscard]] SurfaceBlend dominantBlend(const BlendWeights& weights);

// Shares of the 3 x 3 samples around a sample (row by row, the sample itself in the middle) in
// the mean over its footprint (the square of one pitch centered on it) of the bilinear
// interpolation between the samples: 3/4 and 1/8 along each axis, so 9/16 for the sample, 3/32
// for its 4 side neighbors and 1/64 for its 4 corners. Exact in floats; they sum to 1.
inline constexpr std::array<float, 9> kFootprintShares = {1.0f / 64, 3.0f / 32, 1.0f / 64,
                                                          3.0f / 32, 9.0f / 16, 3.0f / 32,
                                                          1.0f / 64, 3.0f / 32, 1.0f / 64};

// The mean blend over the footprint of a sample from the blends of the 3 x 3 samples around it
// (kFootprintShares), reduced to the dominant ones (dominantBlend).
[[nodiscard]] SurfaceBlend footprintBlend(const std::array<const SurfaceBlend*, 9>& around);

// The blend of a sample whose ground the player edits cover with `material`: the same biomes,
// all the weight of the materials on that one.
[[nodiscard]] BlendTexel withMaterial(BlendTexel texel, SurfaceMaterial material);

} // namespace terrain::horizon

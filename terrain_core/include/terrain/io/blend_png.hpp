#pragma once

// Maps of the transitions between the biomes and the materials (biome_blend.hpp): the colors of
// the biome and material maps (ecology_png.hpp) mixed by the weights of each sample.

#include "terrain/generation/biome_blend.hpp"
#include "terrain/io/ecology_png.hpp"
#include "terrain/io/png.hpp"

#include <span>

namespace terrain {

// The weights of the dominant biome and material of a blend alone, 1 each: the classification,
// before the transitions.
[[nodiscard]] BlendWeights dominantWeights(const SurfaceBlend& blend);
// Every weight of a blend.
[[nodiscard]] BlendWeights blendWeights(const SurfaceBlend& blend);

// The colors of kBiomeRgb (kMaterialRgb) mixed by the weights of the biomes (the materials).
[[nodiscard]] Rgb8 mixedBiomeColor(const BlendWeights& weights);
[[nodiscard]] Rgb8 mixedMaterialColor(const BlendWeights& weights);

// RGB maps of width x height samples, row by row from the top. Throw std::invalid_argument
// unless there are width * height weights.
[[nodiscard]] Image renderMixedBiomeMap(std::span<const BlendWeights> weights, int width,
                                        int height);
[[nodiscard]] Image renderMixedMaterialMap(std::span<const BlendWeights> weights, int width,
                                           int height);

} // namespace terrain

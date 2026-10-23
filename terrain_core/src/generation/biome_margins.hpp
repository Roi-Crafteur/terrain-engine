#pragma once

// Margins of the regions of the biomes and of the surface materials (internal to terrain_core,
// for biome_blend.cpp): the decisions of classifyBiome (biome.cpp) and surfaceMaterial
// (surface_material.cpp) with each threshold widened into a band. biome_blend_test checks that
// the region of the biome and the material they give has a margin of exactly 0 on inputs drawn
// at random and around every threshold: it fails if the two ever diverge.

#include "terrain/generation/biome.hpp"
#include "terrain/generation/biome_blend.hpp"
#include "terrain/generation/climate.hpp"
#include "terrain/generation/surface_material.hpp"
#include "terrain/generation/terrain_surface.hpp"

#include <array>

namespace terrain::blend {

// Margins of the regions of every biome and every material at a column, in widths: 0 inside the
// region the classification gives (exactly 0 for classifyBiome and surfaceMaterial), growing
// away from it, infinite where a sharp condition rules it out (the water, a wall of the cliffs,
// rock above air).
struct Margins {
    std::array<float, kBiomeCount> biomes{};
    std::array<float, kSurfaceMaterialCount> materials{};
};

[[nodiscard]] Margins regionMargins(const ClimateSample& climate, const SurfaceSite& site,
                                    const BiomeParams& biomes, const MaterialParams& materials,
                                    const BlendParams& params);

} // namespace terrain::blend

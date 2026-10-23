#pragma once

#include "horizon_layout.hpp"

#include "terrain/generation/biome.hpp"
#include "terrain/generation/surface_material.hpp"

namespace terrain::unreal {

// What the materials of the ground read besides the relief: the temporary terrain material of
// phase 14 (Scripts/temporary_terrain_look.py), a readable stand-in until the real materials.
// Both the GPU horizon (M_TerrainHorizonGpu) and the tiles (M_TemporaryTerrain) read the surface
// of the ground from the texels of the horizon (horizon_textures.hpp), so the near and the far
// ground take their layers from the same samples.

// The surface of a sample, its material and its biome, packed as the integer
// material + kSurfaceBiomeStride * biome: exact in a float, never negative (the texels of the
// ocean hold a negative ground instead, texelGround).
inline constexpr int kSurfaceBiomeStride = 8;
static_assert(kSurfaceMaterialCount <= kSurfaceBiomeStride, "a material fits below the stride");

struct SurfaceCode {
    SurfaceMaterial material = SurfaceMaterial::Water;
    Biome biome = Biome::Ocean;

    friend bool operator==(const SurfaceCode&, const SurfaceCode&) = default;
};

[[nodiscard]] float packSurface(SurfaceMaterial material, Biome biome);
// The surface of a packed code; codes out of range give the ocean.
[[nodiscard]] SurfaceCode unpackSurface(float code);

// The drop of the GPU horizon below its ground (ATerrainWorld::HorizonDropCm) keeps the tiles in
// front where both overlap, around the hole of level 0. At the outer edge of the tiles it would
// leave a step down to the horizon, whose shadow draws a thin dark line on flat ground under a
// low sun: the drop fades out across the band of tiles over level 0, full in its inner half and
// none from the edge of the area the tiles always cover on. The material computes the same in
// HLSL (Scripts/create_horizon_gpu_material.py): this is its reference.
struct DropFade {
    double coveredMeters = 0.0; // Chebyshev distance from the center of level 0: no drop beyond
    double fadeMeters = 0.0;    // the width of the fade, inward from coveredMeters
};

[[nodiscard]] DropFade dropFade(const TileHole& hole, double level0PitchMeters);

// The share of the drop at this Chebyshev distance from the center of level 0, meters: 1 up to
// coveredMeters - fadeMeters, 0 from coveredMeters on (every vertex of the levels 1 and up),
// linear between.
[[nodiscard]] double dropShare(const DropFade& fade, double chebyshevMeters);

} // namespace terrain::unreal

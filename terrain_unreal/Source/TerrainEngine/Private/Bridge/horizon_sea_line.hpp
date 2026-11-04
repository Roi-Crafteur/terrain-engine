#pragma once

#include "horizon_textures.hpp"

namespace terrain::unreal {

// The line of the sea of the GPU horizon (agent 40): where its material shows the sea along the
// coasts, from the texels of its samples (horizon_textures.hpp). The material
// (Scripts/create_horizon_gpu_material.py, Scripts/horizon_sea_hlsl.py) computes the same in HLSL:
// these are its reference.
//
// - InGround: the material of the ground shows the sea where the ground interpolated between the
//   samples (paintedGround: the bottom under the sea on the ocean) lies below the sea level, on a
//   ground shown at the sea level over the ocean (the texels of the ocean hold kSeaLevel). Along an
//   edge from a sample of the sea (bottom -d) to the land (height H), the sea stops at d / (d + H)
//   of the edge, where the ground shown stands H d / (H + d) above the sea level: about the depth
//   at the foot of a wall, whatever the pitch of the level.
// - Layer: the ground shows its bottom under the sea; a flat layer at the sea level draws the sea,
//   which shows where the ground shown lies below it: the coast is where that ground crosses the
//   sea level, the water never above it.
enum class SeaDrawing { InGround, Layer };

// What the material of the GPU horizon draws today.
inline constexpr SeaDrawing kHorizonSeaDrawing = SeaDrawing::InGround;

// The height of the ground the grid shows at a texel, meters (before the drop and the curvature):
// the height of the texel (the sea level on the ocean) with InGround; with Layer, the ground under
// the sea on the ocean (texelGround).
[[nodiscard]] float shownHeight(const HorizonTexel& texel, SeaDrawing drawing);

// The ground the material of the ground interpolates for its sea (HORIZON_GROUND of its vertex
// shader): texelGround, 1 cm higher on land (land at the sea level stays dry).
[[nodiscard]] float paintedGround(const HorizonTexel& texel);

// The coast along the segment between two vertices of the grid with the texels a (t = 0) and b
// (t = 1): where the sea stops on it, and how high above the sea level the ground shown stands
// there (the water the eye sees standing up the slope; 0 for Layer). None where the sea shows at
// neither end or at both.
struct EdgeCoast {
    bool found = false;
    double t = 0.0;
    double painted = 0.0;
};

[[nodiscard]] EdgeCoast edgeCoast(const HorizonTexel& a, const HorizonTexel& b, SeaDrawing drawing);

} // namespace terrain::unreal

#pragma once

#include "CoreMinimal.h"

#include "Bridge/horizon_textures.hpp"

class UObject;
class UStaticMesh;

// A fixed grid of the GPU horizon as a static mesh built in game (the fast path of
// UStaticMesh::BuildFromMeshDescriptions, without CPU copy): a vertex (i, k) of the grid stands at
// (i, k, 0) in cells, its kind in its vertex color (red: the bottom of a skirt, green: a corner of
// the sea square), normal up. Its bounds reach UpCm above and DownCm below its plane: the material
// moves its vertices by the heights of the texels and the curvature.
UStaticMesh* BuildHorizonGridMesh(UObject& Outer, const terrain::unreal::GridMesh& Grid, FName Name,
	double UpCm, double DownCm);

// The grids of UTerrainHorizonClipmap (Grids): level 0, the ring, then the trims, first with the
// centers of their cells, then without them (TerrainHorizonClipmapSplit.cpp); none where unused.
namespace TerrainHorizonGridSlots
{
	// Bounds of the grids around their plane: the highest ground above, and below the deepest
	// sea, the drop of the curvature at the corner of the coarsest window and its skirt.
	inline constexpr double UpCm = 1.0e6;
	inline constexpr double DownCm = 4.5e6;
	inline constexpr int32 VertexBytes = 32; // position, tangents, color and uv on the GPU
	inline constexpr int32 FirstTrim = 2;
	inline constexpr int32 CenteredTrim = 4; // the finer window at the center of the square
	inline constexpr int32 Shapes = FirstTrim + terrain::unreal::kTrimVariants;
}

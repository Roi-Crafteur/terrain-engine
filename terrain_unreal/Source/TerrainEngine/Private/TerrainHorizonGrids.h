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

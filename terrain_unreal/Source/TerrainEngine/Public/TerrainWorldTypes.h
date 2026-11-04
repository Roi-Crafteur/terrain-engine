#pragma once

#include "CoreMinimal.h"

#include "TerrainWorldTypes.generated.h"

// The enums of the settings of ATerrainWorld (TerrainWorld.h).

// How the tiles become Unreal meshes: the options of phase 10, to compare by measure.
UENUM(BlueprintType)
enum class ETerrainMeshType : uint8
{
	ProceduralMesh UMETA(DisplayName = "Procedural Mesh Component"),
	// Placeholders, not implemented yet: they fall back to ProceduralMesh with a warning.
	DynamicMesh UMETA(DisplayName = "Dynamic Mesh (not implemented)"),
	RealtimeMesh UMETA(DisplayName = "Realtime Mesh Component (not implemented)")
};

// How the far horizon is drawn.
UENUM(BlueprintType)
enum class ETerrainHorizonRenderer : uint8
{
	// Textures written by bands and fixed grids moved by the material (phase 13, step 2).
	Gpu UMETA(DisplayName = "GPU (textures and fixed grids)"),
	// A procedural mesh per level rebuilt on the CPU when it moves (step 1), for comparison;
	// to be removed.
	Cpu UMETA(DisplayName = "CPU (procedural meshes)")
};

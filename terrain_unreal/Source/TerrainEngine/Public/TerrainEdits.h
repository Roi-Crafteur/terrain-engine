#pragma once

#include "CoreMinimal.h"

#include "TerrainEdits.generated.h"

// The shape of an edit of the blocks of the terrain (terrain_core storage/volume_edit.hpp).
UENUM(BlueprintType)
enum class ETerrainEditShape : uint8
{
	Box,
	Sphere,
	// A sphere whose edge is rough; it only removes.
	Explosion
};

// What an edit does to its blocks.
UENUM(BlueprintType)
enum class ETerrainEditMode : uint8
{
	Remove UMETA(DisplayName = "Remove (dig)"),
	Add UMETA(DisplayName = "Add (rock)"),
	Restore UMETA(DisplayName = "Restore (as generated)")
};

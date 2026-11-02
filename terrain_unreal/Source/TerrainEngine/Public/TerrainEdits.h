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

// The tools of breaking and placing in game (UTerrainBuildComponent, Bridge/block_targeting.hpp).
UENUM(BlueprintType)
enum class ETerrainBuildTool : uint8
{
	Block UMETA(DisplayName = "Block 1 m"),
	Sphere2 UMETA(DisplayName = "Sphere 2 m across"),
	Sphere4 UMETA(DisplayName = "Sphere 4 m across")
};

// What a click of the player does.
UENUM(BlueprintType)
enum class ETerrainBuildAction : uint8
{
	Break UMETA(DisplayName = "Break (remove)"),
	Place UMETA(DisplayName = "Place (add rock)")
};

// Breaking and placing in game, on ATerrainWorld (agent 28, TerrainWorldBuild.cpp).
USTRUCT(BlueprintType)
struct FTerrainBuildSettings
{
	GENERATED_BODY()

	// The player pawn gets a UTerrainBuildComponent if it has none (break: left click, place:
	// right click, tools: 1, 2, 3, on QWERTY and AZERTY keyboards).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Edits")
	bool bAddToPlayerPawn = true;

	// Off by default, so that a test or the launcher of the checks never loads an edited world.
	// On: the edits of the seed are read from SaveFolder/seed_<seed> when the world starts and
	// written there on a thread (also the console command Terrain.EditSave 0|1). Never the planet
	// file.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Edits")
	bool bSaveEdits = false;

	// Relative to the Saved directory of the project.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Edits",
		meta = (EditCondition = "bSaveEdits"))
	FString SaveFolder = TEXT("TerrainEngine/Edits");

	// The edits done since the last save are written at most this often, and when the play ends.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Edits",
		meta = (EditCondition = "bSaveEdits", ClampMin = "1"))
	float SaveIntervalSeconds = 10.0f;
};

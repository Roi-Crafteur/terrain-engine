#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Templates/PimplPtr.h"

#include "TerrainWorld.generated.h"

class APawn;
class UMaterialInterface;
class UTerrainMeshBackend;
struct FTerrainWorldState;

// How the tiles become Unreal meshes: the options of phase 10, to compare by measure.
UENUM(BlueprintType)
enum class ETerrainMeshType : uint8
{
	ProceduralMesh UMETA(DisplayName = "Procedural Mesh Component"),
	// Placeholders, not implemented yet: they fall back to ProceduralMesh with a warning.
	DynamicMesh UMETA(DisplayName = "Dynamic Mesh (not implemented)"),
	RealtimeMesh UMETA(DisplayName = "Realtime Mesh Component (not implemented)")
};

/**
 * The smooth procedural world of terrain_core, streamed around the player.
 *
 * On BeginPlay, checks that this build generates the world of terrain_core bit for bit, then
 * creates the tiles and their streaming (worker threads). Each tick streams the tiles around the
 * player: the tiles loaded become meshes (at most MaxTileUploadsPerFrame per frame), the tiles
 * unloaded are removed, the LOD 0 tiles get collision. A new pawn waits for the ground under it:
 * the tiles around it are generated at once and, if it stands below the ground, it is placed
 * on it. The costs of the tiles are logged every TimingLogIntervalSeconds (LogTerrainEngine).
 *
 * terrain_core works in meters with Y up, Unreal in centimeters with Z up: the ground of the core
 * point (CoreOriginX, CoreOriginZ) lies at the Unreal origin (Bridge/unreal_frame.hpp). The actor
 * may stand anywhere: the tiles are placed in world space.
 */
UCLASS(Blueprintable, ClassGroup = (Terrain), meta = (DisplayName = "Terrain World"))
class TERRAINENGINE_API ATerrainWorld : public AActor
{
	GENERATED_BODY()

public:
	ATerrainWorld();

	virtual void Tick(float DeltaSeconds) override;
	// The origin rebasing of Unreal moved the world: the frame of the core follows.
	virtual void ApplyWorldOffset(const FVector& InOffset, bool bWorldShift) override;

	// Seed of the world: the same seed always gives the same world, on every machine.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|World")
	int64 Seed = 20261019;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|World")
	bool bCaves = false;

	// The cave regions (districts, galleries, rivers, lava), with the caves only.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|World",
		meta = (EditCondition = "bCaves"))
	bool bCaveRegions = false;

	// The point of terrain_core (meters, x east, z south) whose ground lies at the Unreal origin.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|World")
	double CoreOriginX = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|World")
	double CoreOriginZ = 0.0;

	// LOD rings, in chunks of 16 m from the chunk of the player: LOD 0 (1 m cells, collision)
	// below Lod0DistanceChunks, LOD 1 below Lod1, LOD 2 below Lod2, LOD 3 up to the view
	// distance. Lod1 - Lod0 >= 2 and Lod2 - Lod1 >= 4.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Streaming",
		meta = (ClampMin = "1"))
	int32 Lod0DistanceChunks = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Streaming",
		meta = (ClampMin = "3"))
	int32 Lod1DistanceChunks = 8;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Streaming",
		meta = (ClampMin = "7"))
	int32 Lod2DistanceChunks = 16;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Streaming",
		meta = (ClampMin = "0"))
	int32 ViewDistanceChunks = 32;

	// Threads generating the tiles, besides the game thread.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Streaming",
		meta = (ClampMin = "1", ClampMax = "16"))
	int32 WorkerThreads = 2;

	// Budget of tiles converted into meshes per frame.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Streaming",
		meta = (ClampMin = "1"))
	int32 MaxTileUploadsPerFrame = 8;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Meshes")
	ETerrainMeshType MeshType = ETerrainMeshType::ProceduralMesh;

	// Material of the tiles. None: /Game/TerrainEngine/M_TerrainAltitude if it exists
	// (Scripts/create_terrain_material.py), otherwise the vertex color material of Unreal.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Meshes")
	TObjectPtr<UMaterialInterface> Material;

	// Cooks the collision of the LOD 0 tiles off the game thread. Off: the ground exists as soon
	// as its tile is shown, at the cost of the cooking in the frame.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Meshes")
	bool bAsyncCollisionCooking = false;

	// The actor the tiles are streamed around. None: the pawn of the first player, or its camera.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Player")
	TObjectPtr<AActor> FocusActor;

	// A new pawn standing below the ground is placed on it.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Player")
	bool bSnapPawnToGround = true;

	// Around a new pawn, the LOD 0 tiles within this many chunks are generated before it moves.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Player",
		meta = (ClampMin = "0", ClampMax = "3"))
	int32 WarmUpRadiusChunks = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Player",
		meta = (ClampMin = "0.1"))
	float WarmUpTimeoutSeconds = 10.0f;

	// Interval of the summary of the tile costs in the log; 0: no summary.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Measures",
		meta = (ClampMin = "0"))
	float TimingLogIntervalSeconds = 5.0f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool StartStreaming();
	void StopStreaming(const FString& Reason);
	bool FindFocus(FVector& OutLocation, APawn*& OutPawn) const;
	void WarmUpAround(APawn& Pawn);
	void SnapToGround(APawn& Pawn) const;
	void LogTimings(float DeltaSeconds);
	UMaterialInterface* ResolveMaterial() const;

	UPROPERTY(Transient)
	TObjectPtr<UTerrainMeshBackend> Backend;

	// The tiles, their streaming and the frame of the core, while playing only (never on the
	// class default object nor in the editor).
	TPimplPtr<FTerrainWorldState> State;
	TWeakObjectPtr<APawn> WarmedPawn;
	float SecondsSinceTimingLog = 0.0f;
};

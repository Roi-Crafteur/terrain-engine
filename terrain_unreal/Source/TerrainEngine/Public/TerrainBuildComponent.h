#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Templates/PimplPtr.h"
#include "TerrainEdits.h"

#include "TerrainBuildComponent.generated.h"

class ATerrainWorld;
class UInputAction;
class UInputMappingContext;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
struct FTerrainBuildRuntime;

/**
 * Breaking and placing in game (agent 28, phase 15), on the pawn of the player: ATerrainWorld gives
 * one to the player pawn (Building.bAddToPlayerPawn), or add it yourself.
 *
 * A trace from the camera finds the tile aimed at, within ReachMeters of the eye of the pawn; the
 * ghost of the edit shows what a click would remove (left click) or add (right click), with the
 * tool chosen by the keys 1, 2, 3 or the wheel: a block of 1 m, a sphere 2 m or 4 m across. Matter
 * is never placed in the capsule of the pawn. The edits go to the edit thread of the session
 * (Bridge/block_targeting.hpp, build_rules.hpp): the tiles are generated again, with their
 * collision and without the plants whose ground is gone. Nothing happens while the dev map is
 * open, while the view loads (a teleport, the start), while the player controls another pawn (the
 * flying pawn of the tools), or without the focus. A held button edits EditsPerSecond times a
 * second at most.
 *
 * Enhanced Input: the actions and their context are made at BeginPlay with the default keys when
 * left empty; give your own (both the context and its actions) to change the keys. The keys other
 * contexts of the player also map are written in the log once.
 */
UCLASS(ClassGroup = (Terrain), meta = (BlueprintSpawnableComponent, DisplayName = "Terrain Build"))
class TERRAINENGINE_API UTerrainBuildComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTerrainBuildComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	// How far from the eye of the pawn matter can be broken or placed, meters.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Build",
		meta = (ClampMin = "0.5", ClampMax = "64"))
	float ReachMeters = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Build")
	ETerrainBuildTool Tool = ETerrainBuildTool::Block;

	// Edits a second at most while a button is held (and for fast clicks).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Build",
		meta = (ClampMin = "0.5", ClampMax = "30"))
	float EditsPerSecond = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Build")
	bool bShowGhost = true;

	// The material of the ghost; none: the wireframe material of the engine.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Build")
	TObjectPtr<UMaterialInterface> GhostMaterial;

	// The input; none: made at BeginPlay (break: left mouse button, place: right mouse button,
	// tools: 1, 2, 3, the next or previous tool: the wheel).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Build|Input")
	TObjectPtr<UInputMappingContext> MappingContext;

	// Above the contexts of the character (0 for most): the buttons of the build come first.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Build|Input")
	int32 MappingPriority = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Build|Input")
	TObjectPtr<UInputAction> BreakAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Build|Input")
	TObjectPtr<UInputAction> PlaceAction;

	// A value along one axis: positive, the next tool; negative, the previous one.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Build|Input")
	TObjectPtr<UInputAction> CycleToolAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Build|Input")
	TObjectPtr<UInputAction> BlockToolAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Build|Input")
	TObjectPtr<UInputAction> SmallSphereToolAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Build|Input")
	TObjectPtr<UInputAction> LargeSphereToolAction;

	// Breaks or places at the point aimed at now, as a click: true if the edit is queued, false
	// with the reason in GetLastRefusal otherwise (nothing within reach, the capsule, the dev map,
	// a load, the rate of the edits...).
	UFUNCTION(BlueprintCallable, Category = "Terrain|Build")
	bool Build(ETerrainBuildAction Action);

	UFUNCTION(BlueprintCallable, Category = "Terrain|Build")
	void SetTool(ETerrainBuildTool NewTool);

	UFUNCTION(BlueprintCallable, Category = "Terrain|Build")
	FString GetLastRefusal() const { return LastRefusal; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void MakeDefaultInput();
	void MapInput();
	void UnmapInput();
	void LogSharedKeys();
	void PollInput();
	void UpdateGhost();
	ATerrainWorld* FindTerrainWorld();
	// The refusal of a click now, or empty when it may edit.
	FString GateRefusal() const;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Ghost;

	// The meshes of the ghost, from the engine.
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeShape;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SphereShape;

	TWeakObjectPtr<ATerrainWorld> TerrainWorld;
	FString LastRefusal;
	// The rate of the edits and the buttons of the last frame (TerrainBuildComponent.cpp).
	TPimplPtr<FTerrainBuildRuntime> Runtime;
};

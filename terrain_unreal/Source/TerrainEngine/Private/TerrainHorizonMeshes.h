#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "Bridge/horizon_streamer.hpp"
#include "Bridge/horizon_timings.hpp"

#include "TerrainHorizonMeshes.generated.h"

class AActor;
class UMaterialInterface;
class UProceduralMeshComponent;

/**
 * The meshes of the far horizon of an ATerrainWorld: one UProceduralMeshComponent per level of
 * the clipmap, without collision, its body in section 0 and its trim in section 1, placed in
 * world space at the low corner of the window of its level (Bridge/horizon_streamer.hpp). After
 * its first upload, a body only replaces its vertices (UpdateMeshSection): its triangles never
 * change. The material curves the horizon (Scripts/create_horizon_material.py): it receives the
 * radius of the planet in its scalar parameter PlanetRadiusCm.
 */
UCLASS(Transient)
class UTerrainHorizonMeshes : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(AActor& Owner, UMaterialInterface* Material, double PlanetRadiusCm,
		bool bCastShadows);

	// Applies an update of the horizon within this frame: the uploads (timed), then the moves.
	void Apply(const terrain::unreal::HorizonUpdate& Update,
		terrain::unreal::HorizonTimings& Timings);

	void RemoveAll();

	// The tools of the developers (Terrain.View): every level hidden in game or shown.
	void SetHiddenByTools(bool bHidden);

	// Vertices and triangles of the meshes shown, and the bytes the procedural meshes keep of
	// them on the CPU (an estimate from the size of their vertex).
	int64 GetVertexCount() const;
	int64 GetTriangleCount() const;
	int64 GetMeshBytes() const;

	// Name of the scalar parameter of the material of the horizon that holds the radius.
	static const FName PlanetRadiusParameter;

private:
	UProceduralMeshComponent* LevelComponent(int32 Level);
	void Upload(const terrain::unreal::HorizonUpload& Upload,
		terrain::unreal::HorizonTimings& Timings);

	UPROPERTY()
	TObjectPtr<AActor> OwnerActor;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> HorizonMaterial;

	UPROPERTY()
	TArray<TObjectPtr<UProceduralMeshComponent>> Levels;

	// Per level and section: the vertices and triangles shown.
	TArray<FIntPoint> SectionSizes;
	bool bShadows = false;
	bool bHiddenByTools = false; // the levels created from now on too
};

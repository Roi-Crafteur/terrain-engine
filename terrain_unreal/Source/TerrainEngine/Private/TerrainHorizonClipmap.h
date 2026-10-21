#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"
#include "UObject/Object.h"

#include "Bridge/horizon_gpu_streamer.hpp"
#include "Bridge/horizon_timings.hpp"

#include "TerrainHorizonClipmap.generated.h"

class AActor;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UTexture2D;

// How the GPU horizon looks: what its material needs besides the textures.
struct FTerrainHorizonLook
{
	double DropCm = 100.0;
	double MorphCells = 32.0;
	bool bShadows = false;
	bool bDebugTint = false; // each level tinted with its own hue
};

/**
 * The far horizon drawn by the GPU (phase 13, step 2): per level, a texture of 257 x 257 texels
 * (float RGBA: height, slopes, color; Bridge/horizon_textures.hpp) written by bands, and fixed
 * grids built once as static meshes (the body of level 0, the body shared by the other levels,
 * the 9 trims shared by them), drawn by M_TerrainHorizonGpu
 * (Scripts/create_horizon_gpu_material.py) which reads the texels, morphs toward the coarser level
 * near the border and curves the planet.
 * A move of a level writes the texels of its new band and sets a few parameters: nothing is
 * rebuilt, nothing is kept on the CPU but the samples of the clipmap.
 */
UCLASS(Transient)
class UTerrainHorizonClipmap : public UObject
{
	GENERATED_BODY()

public:
	// Builds the grids, the textures, the materials and the components of every level.
	void Initialize(AActor& Owner, UMaterialInterface* Material,
		const terrain::unreal::HorizonGpuStreamer& Streamer, const FTerrainHorizonLook& Look);

	// Applies an update within this frame: the texels (timed, handed over to the render thread),
	// then the placements.
	void Apply(terrain::unreal::HorizonGpuUpdate& Update, terrain::unreal::HorizonTimings& Timings);

	void RemoveAll();

	// Time spent building the grids, and the bytes of the textures and of the grids on the GPU.
	double GetBuildMilliseconds() const { return BuildMilliseconds; }
	int64 GetGpuBytes() const { return GpuBytes; }
	int64 GetVertexCount() const { return Vertices; }
	int64 GetTriangleCount() const { return Triangles; }

private:
	UStaticMeshComponent* CreateGridComponent(UStaticMesh* Mesh, UMaterialInstanceDynamic* Look);
	void CreateLevel(int32 Level, const terrain::unreal::HorizonGpuStreamer& Streamer,
		UMaterialInterface* Material);
	void UploadRegion(terrain::unreal::TexelRegion& Region);
	void Place(const terrain::unreal::LevelPlacement& Placement);

	UPROPERTY()
	TObjectPtr<AActor> OwnerActor;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMesh>> Grids; // level 0, the ring, then the 9 trims

	UPROPERTY()
	TArray<TObjectPtr<UTexture2D>> Textures;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Looks;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Bodies;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Trims; // none for level 0

	TArray<int32> TrimShown;
	TArray<TOptional<terrain::unreal::LevelPlacement>> Placed;
	FTerrainHorizonLook HorizonLook;
	int32 Side = 0;
	double BuildMilliseconds = 0.0;
	int64 GpuBytes = 0;
	int64 Vertices = 0;
	int64 Triangles = 0;
};

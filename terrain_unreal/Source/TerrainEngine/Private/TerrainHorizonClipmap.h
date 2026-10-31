#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"
#include "UObject/Object.h"

#include "Bridge/horizon_atmosphere.hpp"
#include "Bridge/horizon_gpu_streamer.hpp"
#include "Bridge/horizon_timings.hpp"
#include "Bridge/terrain_surface.hpp"

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
 * With the detail normals (Bridge/horizon_normal_stream.hpp), a second texture per level of
 * (257 * ratio)^2 texels (half floats: the slopes) lights the levels and the tiles finer than
 * their grids; its texels arrive after the samples, pending until then.
 * The water (task 7b): a texture of 257 x 257 floats per level (R32F, the level of the core over
 * each sample, Bridge/horizon_textures.hpp), written with the texels and bound as HorizonWater and
 * CoarseWater: the material raises the lakes and rivers to their level (agent 27,
 * Scripts/horizon_water_hlsl.py). The textures: TerrainHorizonTextures.h.
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

	// Where the waves of the sea are measured from (Bridge/horizon_atmosphere.hpp), every level and
	// the surface looks of the tiles.
	void SetWaveOrigin(const terrain::unreal::WaveOrigin& Origin);

	// The lakes and rivers drawn at their level, or not (the ground under them), every level.
	void SetInlandWater(bool bShown);

	// The parameters of the look of the water that Source has, among Scalars and Vectors, set on
	// every level: the lakes and rivers of the horizon look as those of the tiles (one tuning).
	void CopyLook(const UMaterialInterface& Source, TConstArrayView<FName> Scalars,
		TConstArrayView<FName> Vectors);

	// A material of the tiles that follows level 0 from now on: the material of their ground reads
	// the surface of the ground (material and biome) from the texels of level 0 (the temporary
	// terrain material, Bridge/terrain_surface.hpp); the material of their water fades into the
	// water of the horizon over the outer band of the tiles (Scripts/water_look_hlsl.py). Both
	// read the same parameters (SurfaceOrigin, SurfaceCells, SurfacePitchCm, SurfaceCoveredCm,
	// SurfaceBandCm, HasSurface...); nothing without a look.
	void BindSurfaceLook(UMaterialInstanceDynamic* Look);

	// Time spent building the grids, and the bytes of the textures and of the grids on the GPU.
	double GetBuildMilliseconds() const { return BuildMilliseconds; }
	int64 GetGpuBytes() const { return GpuBytes; }
	int64 GetVertexCount() const { return Vertices; }
	int64 GetTriangleCount() const { return Triangles; }

private:
	UStaticMeshComponent* CreateGridComponent(UStaticMesh* Mesh, UMaterialInstanceDynamic* Look);
	void CreateLevel(int32 Level, const terrain::unreal::HorizonGpuStreamer& Streamer,
		UMaterialInterface* Material);
	void BindNormals(int32 Level);
	void Place(const terrain::unreal::LevelPlacement& Placement);
	void PlaceSurface(const terrain::unreal::LevelPlacement& Placement);

	UPROPERTY()
	TObjectPtr<AActor> OwnerActor;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMesh>> Grids; // level 0, the ring, then the 9 trims

	UPROPERTY()
	TArray<TObjectPtr<UTexture2D>> Textures;

	UPROPERTY()
	TArray<TObjectPtr<UTexture2D>> WaterTextures; // the water levels, one per level

	UPROPERTY()
	TArray<TObjectPtr<UTexture2D>> NormalTextures; // the detail normals, none without them

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Looks;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Bodies;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Trims; // none for level 0

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SurfaceLooks; // of the tiles: ground, water

	TArray<int32> TrimShown;
	TArray<TOptional<terrain::unreal::LevelPlacement>> Placed;
	FTerrainHorizonLook HorizonLook;
	terrain::unreal::DropFade Fade;
	double Level0PitchCm = 0.0;
	int32 Side = 0;
	int32 NormalRatio = 0; // detail texels per sample along each axis, 0 without the detail
	double BuildMilliseconds = 0.0;
	int64 GpuBytes = 0;
	int64 Vertices = 0;
	int64 Triangles = 0;
};

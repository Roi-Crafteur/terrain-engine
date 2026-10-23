#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Templates/PimplPtr.h"

#include "TerrainWorld.generated.h"

class AExponentialHeightFog;
class APawn;
class ASkyAtmosphere;
class ASkyLight;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTerrainAtmosphereFollower;
class UTerrainHorizonClipmap;
class UTerrainHorizonMeshes;
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

	// Material of the tiles. None: /Game/TerrainEngine/M_TemporaryTerrain if it exists (the
	// temporary terrain material, Scripts/create_temporary_terrain_material.py), else
	// /Game/TerrainEngine/M_TerrainAltitude (Scripts/create_terrain_material.py), otherwise the
	// vertex color material of Unreal. A material with the texture parameter SurfaceTexels reads
	// the material and the biome of the ground from the texels of the GPU horizon.
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

	// The far horizon: the levels of the clipmap of terrain_core around the player, from under the
	// tiles up to 520 km with 10 levels, curved by its material. The tiles must end inside its
	// first level: ViewDistanceChunks from 28 to 55 with the default LOD rings.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Horizon")
	bool bHorizon = true;

	// Levels of the horizon: samples every 8 m on the first, twice as far apart on each next one.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Horizon",
		meta = (EditCondition = "bHorizon", ClampMin = "1", ClampMax = "10"))
	int32 HorizonLevels = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Horizon",
		meta = (EditCondition = "bHorizon"))
	ETerrainHorizonRenderer HorizonRenderer = ETerrainHorizonRenderer::Gpu;

	// GPU: pieces of the horizon computed at once as low priority tasks of Unreal, below the
	// workers of the tiles. CPU: threads computing the horizon, besides those of the tiles.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Horizon",
		meta = (EditCondition = "bHorizon", ClampMin = "1", ClampMax = "8"))
	int32 HorizonThreads = 2;

	// Material of the GPU horizon. None: /Game/TerrainEngine/M_TerrainHorizonGpu
	// (Scripts/create_horizon_gpu_material.py); without it, the CPU horizon is drawn instead.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Horizon",
		meta = (EditCondition = "bHorizon"))
	TObjectPtr<UMaterialInterface> HorizonGpuMaterial;

	// GPU: width of the band, in cells of each level, where it blends into the next coarser one.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Horizon",
		meta = (EditCondition = "bHorizon", ClampMin = "1", ClampMax = "100"))
	float HorizonMorphCells = 32.0f;

	// GPU: each level tinted with its own hue, to see the levels and their blends.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Horizon",
		meta = (EditCondition = "bHorizon"))
	bool bHorizonDebugTint = false;

	// Material of the horizon. None: /Game/TerrainEngine/M_TerrainHorizon if it exists
	// (Scripts/create_horizon_material.py), which curves it; otherwise the material of the tiles,
	// flat. Its scalar parameter PlanetRadiusCm receives the radius of the planet.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Horizon",
		meta = (EditCondition = "bHorizon"))
	TObjectPtr<UMaterialInterface> HorizonMaterial;

	// How far below its ground the horizon lies: where the tiles cover it, they stay in front.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Horizon",
		meta = (EditCondition = "bHorizon", ClampMin = "0"))
	float HorizonDropCm = 100.0f;

	// The ocean flat at the sea level on the horizon, and a sea square under the tiles.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Horizon",
		meta = (EditCondition = "bHorizon"))
	bool bHorizonSea = true;

	// Radius of the planet for the curvature of the horizon, km. 0: the circumference of the
	// world along x (about 9,554 km by default).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Horizon",
		meta = (EditCondition = "bHorizon", ClampMin = "0"))
	double PlanetRadiusKm = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Horizon",
		meta = (EditCondition = "bHorizon"))
	bool bHorizonShadows = false;

	// Detail normals of the GPU horizon (Bridge/horizon_normal_stream.hpp): the slopes of the
	// ground HorizonNormalRatio times finer than the grid of each level (2 m on level 0, 16 m on
	// level 3 at 4), which light the levels and the far tiles: crests, walls and gullies stay
	// visible kilometers away. Computed after the samples, by HorizonNormalJobs low priority
	// tasks; the grid normals show until then. Off: the normals of the grids, as before.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Horizon",
		meta = (EditCondition = "bHorizon"))
	bool bHorizonDetailNormals = true;

	// Detail texels per sample along each axis: 2, 4 or 8 (the cost grows with its square).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Horizon",
		meta = (EditCondition = "bHorizon && bHorizonDetailNormals", ClampMin = "2",
			ClampMax = "8"))
	int32 HorizonNormalRatio = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Horizon",
		meta = (EditCondition = "bHorizon && bHorizonDetailNormals", ClampMin = "1",
			ClampMax = "8"))
	int32 HorizonNormalJobs = 2;

	// Crests of the far mountains (terrain_core streaming/horizon_crests.hpp): from level 1 on, the
	// samples rise to the crests of their cells, sought HorizonCrestRatio times finer than each
	// level, so that the summits and ridges keep their height against the sky. 2 by default; 4
	// or 8 seek them finer, at a higher cost of the epochs. 0: the point samples, the horizon as
	// before.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Horizon",
		meta = (EditCondition = "bHorizon", ClampMin = "0", ClampMax = "8"))
	int32 HorizonCrestRatio = 2;

	// The sky follows the camera over the curved planet (terrain_unreal/README.md, section 12):
	// the SkyAtmosphere of the level takes the radius of the planet, its top at the sea level
	// right under the camera, the ExponentialHeightFog stays under the camera at its height above
	// the sea level, and the SkyLight (its real-time capture) on the camera; they move after the
	// camera, every frame. The aerial perspective then reaches the end of the horizon. Off: they
	// stay where the level puts them, and the sky turns black thousands of kilometers away.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Atmosphere")
	bool bAtmosphereFollowsCamera = true;

	// The SkyAtmosphere that follows the camera. None: the first one of the level.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Atmosphere",
		meta = (EditCondition = "bAtmosphereFollowsCamera"))
	TObjectPtr<ASkyAtmosphere> SkyAtmosphere;

	// The ExponentialHeightFog that follows the camera. None: the first one of the level.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Atmosphere",
		meta = (EditCondition = "bAtmosphereFollowsCamera"))
	TObjectPtr<AExponentialHeightFog> HeightFog;

	// The SkyLight that follows the camera. None: the first one of the level.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Atmosphere",
		meta = (EditCondition = "bAtmosphereFollowsCamera"))
	TObjectPtr<ASkyLight> SkyLight;

	// Distance at which the height fog stops integrating (its End Distance), km; 0: its own
	// setting. The fog is flat and the horizon curved: tens of kilometers away the ground lies
	// far below its height, where it would hide it completely.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain|Atmosphere",
		meta = (EditCondition = "bAtmosphereFollowsCamera", ClampMin = "0"))
	float FogEndDistanceKm = 20.0f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool StartStreaming();
	void StopStreaming(const FString& Reason);
	void StartHorizon();
	bool StartGpuHorizon();
	void StartCpuHorizon();
	void StopHorizon(const FString& Reason);
	void TickHorizon(const FVector& Focus);
	void RecordFrame();
	void LogHorizon();
	UMaterialInterface* ResolveHorizonMaterial() const;
	UMaterialInterface* ResolveHorizonGpuMaterial() const;
	void StartAtmosphere();
	void StopAtmosphere();
	double ExtendAerialPerspective();
	double PlanetRadiusMeters() const;
	void FollowFrame();
	bool FindFocus(FVector& OutLocation, APawn*& OutPawn) const;
	void ReturnFromWorldTurn(AActor& Traveller, FVector& InOutFocus) const;
	void WarmUpAround(APawn& Pawn);
	void SnapToGround(APawn& Pawn) const;
	void LogTimings(float DeltaSeconds);
	UMaterialInterface* ResolveMaterial() const;
	UMaterialInterface* MakeTileLook();

	UPROPERTY(Transient)
	TObjectPtr<UTerrainMeshBackend> Backend;

	UPROPERTY(Transient)
	TObjectPtr<UTerrainHorizonMeshes> HorizonMeshes;

	UPROPERTY(Transient)
	TObjectPtr<UTerrainHorizonClipmap> HorizonClipmap;

	UPROPERTY(Transient)
	TObjectPtr<UTerrainAtmosphereFollower> AtmosphereFollower;

	// The instance of the material of the tiles that reads the surface of the ground, if it does.
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TileSurfaceLook;

	// The tiles, their streaming and the frame of the core, while playing only (never on the
	// class default object nor in the editor).
	TPimplPtr<FTerrainWorldState> State;
	TWeakObjectPtr<APawn> WarmedPawn;
	float SecondsSinceTimingLog = 0.0f;
	// The depth of the aerial perspective before StartAtmosphere extended it, km; -1: unchanged.
	float RestoredAerialPerspectiveDepthKm = -1.0f;
};

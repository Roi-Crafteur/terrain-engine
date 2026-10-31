#pragma once

#include "CoreMinimal.h"

#include "TerrainLoadingSettings.generated.h"

// A prepared point is ready (ATerrainWorld::PrepareLocation): the tiles around it and the levels 0
// to 3 of its horizon are shown. Its core coordinates, meters, and the seconds it took.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FTerrainLocationReady, double, CoreX, double, CoreZ,
	float, Seconds);

// The loads of the world around the player (agent 38, TerrainWorldLoading*.cpp,
// terrain_unreal/LOADING.md): at the start, after a teleport and around a prepared point.
USTRUCT(BlueprintType)
struct FTerrainLoadingSettings
{
	GENERATED_BODY()

	// While loading, more tile workers and horizon pieces (together at most ThreadCap) and more
	// uploads per frame, then back to WorkerThreads, HorizonThreads and MaxTileUploadsPerFrame.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loading")
	bool bBoost = true;

	// 0: min(hardware threads - 4, 12), at least WorkerThreads + HorizonThreads.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loading",
		meta = (EditCondition = "bBoost", ClampMin = "0", ClampMax = "64"))
	int32 ThreadCap = 0;

	// While loading, the uploads of a frame take about this long at the mean cost of an upload,
	// between MaxTileUploadsPerFrame and MaxUploadsPerFrame tiles.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loading",
		meta = (EditCondition = "bBoost", ClampMin = "0"))
	float UploadMillisecondsPerFrame = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loading",
		meta = (EditCondition = "bBoost", ClampMin = "1"))
	int32 MaxUploadsPerFrame = 32;

	// The screen held black until the ground under the feet, the 64 m ring, every tile and the
	// levels 0 to 3 of the horizon are shown, FadeMaxSeconds at most (what was missing then is
	// logged). The pawn is held still until the collision under it exists, however long it takes.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loading")
	bool bFade = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loading",
		meta = (EditCondition = "bFade", ClampMin = "0"))
	float FadeMaxSeconds = 2.0f;

	// The boost ends then, whatever is still loading.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loading", meta = (ClampMin = "0"))
	float TimeoutSeconds = 30.0f;

	// A new place brought level by level, finest first (level 0 first), each level shown once
	// computed and coming in over HorizonRevealSeconds; the coarsest level shown dissolves its
	// outer band into the sky while the coarser ones are still to come. Off: every level at once.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loading")
	bool bHorizonLevelByLevel = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loading", meta = (ClampMin = "0"))
	float HorizonRevealSeconds = 0.6f;
};

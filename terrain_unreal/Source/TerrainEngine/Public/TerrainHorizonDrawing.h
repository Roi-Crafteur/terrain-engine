#pragma once

#include "CoreMinimal.h"

#include "TerrainHorizonDrawing.generated.h"

// How the far horizon is drawn (agent 40): its switches, apart from TerrainWorld.h. None of them
// changes a bit of what terrain_core computes, only what Unreal draws of it.
USTRUCT(BlueprintType)
struct FTerrainHorizonDrawing
{
	GENERATED_BODY()

	// The levels of the GPU horizon that cast shadows, finest first: 0, none (as before); n, the
	// levels 0 to n - 1 (4: up to 8 km). The shadows of the tiles then go on beyond them, their
	// cost in the shadow passes (the console command Terrain.HorizonShadows of the tools sets it
	// while playing, to compare). bHorizonShadows still makes every level cast them.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Horizon",
		meta = (ClampMin = "0", ClampMax = "4"))
	int32 ShadowLevels = 0;
};

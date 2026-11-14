#pragma once

#include "CoreMinimal.h"

#include "TerrainHorizonDrawing.generated.h"

// How the far horizon is drawn (agent 40): its switches, apart from TerrainWorld.h. None of them
// changes a bit of what terrain_core computes, only what Unreal draws of it.
USTRUCT(BlueprintType)
struct FTerrainHorizonDrawing
{
	GENERATED_BODY()

	// The shadows of the GPU horizon under a low sun (Bridge/horizon_low_sun.hpp): its levels 0 to 3
	// (up to 8 km) cast shadows while the sun of the atmosphere is lower than this many degrees
	// (off again one degree above it), none otherwise; 0, never. The shadows of the tiles then go
	// on over the far plain (the long shadows of a low sun), their cost in the shadow passes paid
	// only then. tests/horizon_low_sun_study_test.cpp chose the default. bHorizonShadows still
	// makes every level cast them under any sun; the console command Terrain.HorizonShadows of the
	// tools forces them on or off while playing.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Horizon",
		meta = (ClampMin = "0", ClampMax = "45"))
	float LowSunShadowDegrees = 12.0f;
};

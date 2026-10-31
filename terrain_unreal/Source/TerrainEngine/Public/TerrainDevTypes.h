#pragma once

#include "CoreMinimal.h"

#include "TerrainDevTypes.generated.h"

// How the eye of a view is given (the places of data/dev/places.json, their eyeMode): an altitude
// of terrain_core, or a height above the ground or the water under it.
UENUM(BlueprintType)
enum class ETerrainEyeMode : uint8
{
	Altitude UMETA(DisplayName = "Altitude (core meters)"),
	Ground UMETA(DisplayName = "Above the ground or the water")
};

#pragma once

#include "CoreMinimal.h"

#include "TerrainExperimentalSettings.generated.h"

// The experimental flags of the world (agent 51, terrain/generation/experimental_flags.hpp): the
// relief of a wave merged switched off, switched on here by name until it is on for all.
USTRUCT(BlueprintType)
struct FTerrainExperimentalSettings
{
	GENERATED_BODY()

	// Names apart by commas or spaces, such as "seas,volcanoes": they set the relief, the planet
	// level, the water and the ecology of the session. Empty: the world of main. An unknown name
	// is a warning in the log at the start; a planet file built with other flags is refused (an
	// error in the log, nothing built over it): build it with terrain_make_planet --flags.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Experimental")
	FString ExperimentalFlags;
};

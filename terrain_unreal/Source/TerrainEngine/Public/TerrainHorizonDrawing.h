#pragma once

#include "CoreMinimal.h"

#include "TerrainHorizonDrawing.generated.h"

// How the far horizon is drawn (agent 40): its switches, apart from TerrainWorld.h. None of them
// changes a bit of what terrain_core computes, only what Unreal draws of it.
USTRUCT(BlueprintType)
struct FTerrainHorizonDrawing
{
	GENERATED_BODY()

	// The shadows of the GPU horizon under a low sun (Bridge/horizon_low_sun.hpp): its levels 0
	// to 3 (up to 8 km) cast shadows while the sun of the atmosphere is lower than this many degrees
	// (off again one degree above it), none otherwise; 0, never. The shadows of the tiles then go
	// on over the far plain (the long shadows of a low sun), their cost in the shadow passes paid
	// only then. tests/horizon_low_sun_study_test.cpp chose the default. bHorizonShadows still
	// makes every level cast them under any sun; the console command Terrain.HorizonShadows of the
	// tools forces them on or off while playing.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Horizon",
		meta = (ClampMin = "0", ClampMax = "45"))
	float LowSunShadowDegrees = 12.0f;

	// The height fog of the map kept at the sea level under the camera while playing
	// (Bridge/horizon_atmosphere.hpp: heightFogAboveSeaCm): the haze of the sea and of the low
	// coasts, whatever height the map gives it; set back where the map placed it at the end of the
	// play. Off: the Z of the fog in the map, read as its height above the sea level (as before).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Horizon|Haze")
	bool bSeaFog = true;

	// The haze of the dust over dry land (Bridge/atmosphere_haze.hpp, TerrainAtmosphereHaze.h): the
	// dryness of the region around the camera (0 over humid lands, the sea and the cold; 1 over a
	// desert, from the planet level) turns the SkyAtmosphere of the map toward a dusty air while
	// playing: a beige haze over the far sand instead of the lavender band of the aerial
	// perspective. Nothing changes where the dryness is 0, and the values of the map come back at
	// the end of the play. The console command Terrain.Haze of the tools forces it or turns it off.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Horizon|Haze")
	bool bDustHaze = true;

	// The dusty air at a dryness of 1: the depth of the aerial perspective times this...
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Horizon|Haze",
		meta = (EditCondition = "bDustHaze", ClampMin = "0.05", ClampMax = "1"))
	float DustViewDistanceScale = 0.4f;

	// ... its scattering of Mie times this, and its absorption of Mie (1/km, strongest in the
	// blue). Kept small: the sun through it changes by at most 2 % (atmosphere_haze_test).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Horizon|Haze",
		meta = (EditCondition = "bDustHaze", ClampMin = "1", ClampMax = "4"))
	float DustMieScattering = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Horizon|Haze",
		meta = (EditCondition = "bDustHaze", ClampMin = "0", ClampMax = "0.01"))
	float DustMieAbsorptionScale = 0.0006f;

	// The warm bounce of the ground in the shade (Bridge/ground_bounce.hpp,
	// TerrainAtmosphereBounce.h): while playing, the SkyLight takes its lower hemisphere from its capture and the ground of
	// the SkyAtmosphere is given this albedo (linear), so that the faces in the shade get the warm
	// light the sunlit ground sends back. The values of the map come back at the end of the play
	// and with the console command Terrain.Bounce 0 of the tools.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Horizon|Haze")
	bool bGroundBounce = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Horizon|Haze",
		meta = (EditCondition = "bGroundBounce"))
	FLinearColor BounceGroundAlbedo = FLinearColor(0.45f, 0.38f, 0.30f, 1.0f);
};

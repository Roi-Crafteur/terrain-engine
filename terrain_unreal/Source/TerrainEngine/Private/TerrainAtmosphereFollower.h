#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "Bridge/tile_timings.hpp"
#include "Bridge/unreal_frame.hpp"

#include "TerrainAtmosphereFollower.generated.h"

class UExponentialHeightFogComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;

/**
 * Keeps the planet of the SkyAtmosphere under the camera, its top at the sea level
 * (Bridge/horizon_atmosphere.hpp: planetTopUnder), the height fog under the camera at its height
 * above the sea level, and the SkyLight on the camera: its real-time capture renders the sky from
 * where it stands, black from space. Ticks after the camera has moved (TG_PostUpdateWork): the
 * frame drawn after a world turn or the origin rebasing already has its sky. Moving these
 * components only sends their new transform to the renderer.
 */
UCLASS(Transient)
class UTerrainAtmosphereFollower : public UActorComponent
{
	GENERATED_BODY()

public:
	UTerrainAtmosphereFollower();

	// Any may be null. FogAboveSeaCm: the height of the fog above the sea level, kept.
	void Follow(USkyAtmosphereComponent* InSky, UExponentialHeightFogComponent* InFog,
		USkyLightComponent* InLight, double InFogAboveSeaCm);

	// The frame of the core: where the sea level lies (it moves with the origin rebasing).
	void SetFrame(const terrain::unreal::UnrealFrame& InFrame) { Frame = InFrame; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	// Places the sky, the fog and the sky light for a camera at this location.
	void PlaceFor(const FVector& Camera);

	// The cost of the ticks since the last call, on the game thread.
	terrain::unreal::StageStats TakeTimings();

private:
	bool FindCamera(FVector& OutLocation) const;

	TWeakObjectPtr<USkyAtmosphereComponent> Sky;
	TWeakObjectPtr<UExponentialHeightFogComponent> Fog;
	TWeakObjectPtr<USkyLightComponent> Light;
	terrain::unreal::UnrealFrame Frame;
	double FogAboveSeaCm = 0.0;
	terrain::unreal::StageStats Timings;
};

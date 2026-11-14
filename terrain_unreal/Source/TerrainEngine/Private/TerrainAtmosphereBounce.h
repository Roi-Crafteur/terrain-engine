#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include "Bridge/ground_bounce.hpp"

#include <array>

class USkyAtmosphereComponent;
class USkyLightComponent;
struct FTerrainHorizonDrawing;

/**
 * The warm bounce of the ground in the shade (agent 40, Bridge/ground_bounce.hpp), held by the
 * follower of the atmosphere (TerrainAtmosphereFollower.h): the lower hemisphere of the SkyLight
 * taken from its capture (bLowerHemisphereIsBlack false) and the ground of the SkyAtmosphere
 * given a warm albedo (GroundAlbedo), read and set by the names of their properties. The values of
 * the map are kept and set back when the atmosphere stops (Stop) and by Terrain.Bounce 0; the map
 * is never saved.
 */
class FTerrainGroundBounce
{
public:
	// Reads the values of the map and sets the bounce of the settings (bGroundBounce). Off without
	// the SkyLight or the SkyAtmosphere, or without one of their properties (logged).
	void Start(USkyLightComponent* InLight, USkyAtmosphereComponent* InSky,
		const FTerrainHorizonDrawing& Settings);
	// On (the warm ground of the settings or of Terrain.Bounce) or off (the map's values).
	void Set(bool bOn, const std::array<double, 3>& Albedo);
	void Stop();
	// The values on the SkyLight and on the SkyAtmosphere, read back.
	FString Describe() const;
	const std::array<double, 3>& Warm() const { return WarmAlbedo; }

private:
	void Apply(const terrain::unreal::BounceLook& Look);

	TWeakObjectPtr<USkyLightComponent> Light;
	TWeakObjectPtr<USkyAtmosphereComponent> Sky;
	terrain::unreal::BounceLook Map;
	TOptional<FColor> MapAlbedoRaw; // the map's GroundAlbedo as it was (an FColor), set back as is
	std::array<double, 3> WarmAlbedo{0.45, 0.38, 0.30};
	bool bReady = false;
	bool bOnNow = false;
};

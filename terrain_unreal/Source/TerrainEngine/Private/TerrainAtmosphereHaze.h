#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include "Bridge/atmosphere_haze.hpp"

#include <memory>
#include <optional>

class USkyAtmosphereComponent;
struct FTerrainHorizonDrawing;

/**
 * The haze of the dust over dry land (agent 40, Bridge/atmosphere_haze.hpp), held by the follower
 * of the atmosphere (TerrainAtmosphereFollower.h), which has the camera every frame: the dryness of
 * the region around the camera, from the planet level, turns the SkyAtmosphere toward a dusty air
 * (less depth of aerial perspective, a little more scattering and absorption of Mie). Its values
 * are read and set by the names of their properties (as Terrain.Aerial does), only when the
 * dryness shown moves; the values of the map are kept and set back at the end (Stop). The map is
 * never saved.
 */
class FTerrainAtmosphereHaze
{
public:
	// Reads the values of the map on the sky and the dusty air of the settings. Off without the
	// sky, without one of its properties (logged) or without the planet level.
	void Start(USkyAtmosphereComponent* InSky, std::shared_ptr<const terrain::PlanetLevel> InPlanet,
		const FTerrainHorizonDrawing& Settings);
	// Each frame: the camera at (x, z) of the core, the time since the last frame.
	void Update(double CoreX, double CoreZ, double DeltaSeconds);
	// The values of the map set back on the sky, with a line of log.
	void Stop();

	// Terrain.Haze: the region (auto), the values of the map (off), or a dryness forced; and the
	// dusty air changed while playing.
	void SetMode(bool bInOn, std::optional<double> InForced);
	terrain::unreal::DustHaze& Dust() { return DustAir; }
	// The dryness shown and the values on the sky, read back.
	FString Describe() const;

private:
	void Apply(const terrain::unreal::HazeLook& Look);

	TWeakObjectPtr<USkyAtmosphereComponent> Sky;
	std::shared_ptr<const terrain::PlanetLevel> Planet;
	terrain::unreal::HazeLook Map;
	terrain::unreal::DustHaze DustAir;
	terrain::unreal::HazeFollower Follower;
	std::optional<double> Forced;
	bool bReady = false; // the values of the map read
	bool bOn = true;
	double Target = 0.0;
	double SinceRefresh = 1.0e9;
	double Applied = -1.0; // the dryness last set; -1: the values of the map
};

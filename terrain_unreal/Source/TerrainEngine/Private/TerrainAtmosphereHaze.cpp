// The haze of the dust over dry land (agent 40): see TerrainAtmosphereHaze.h and
// Bridge/atmosphere_haze.hpp.
#include "TerrainAtmosphereHaze.h"

#include "Components/SkyAtmosphereComponent.h"
#include "TerrainEngineLog.h"
#include "TerrainHorizonDrawing.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "terrain/generation/planet/planet_level.hpp"

#include <cmath>
#include <utility>

namespace
{
	// The values of the haze on the SkyAtmosphere, by the names of their properties (Unreal keeps
	// the typo of AerialPespectiveViewDistanceScale).
	const TCHAR* const HazeViewDistance = TEXT("AerialPespectiveViewDistanceScale");
	const TCHAR* const HazeMieScattering = TEXT("MieScatteringScale");
	const TCHAR* const HazeMieAbsorption = TEXT("MieAbsorptionScale");
	const TCHAR* const HazeMieAbsorptionColor = TEXT("MieAbsorption");

	FFloatProperty* HazeFloat(const USkyAtmosphereComponent& SkyComponent, const TCHAR* Name)
	{
		return FindFProperty<FFloatProperty>(SkyComponent.GetClass(), Name);
	}

	FLinearColor* HazeColor(USkyAtmosphereComponent& SkyComponent)
	{
		FStructProperty* Property =
			FindFProperty<FStructProperty>(SkyComponent.GetClass(), HazeMieAbsorptionColor);
		if (Property == nullptr || Property->Struct != TBaseStructure<FLinearColor>::Get())
		{
			return nullptr;
		}
		return Property->ContainerPtrToValuePtr<FLinearColor>(&SkyComponent);
	}

	// The first property of the haze this version of Unreal does not have, or nullptr.
	const TCHAR* MissingHazeProperty(USkyAtmosphereComponent& SkyComponent)
	{
		const TCHAR* const Floats[] = {HazeViewDistance, HazeMieScattering, HazeMieAbsorption};
		for (const TCHAR* Name : Floats)
		{
			if (HazeFloat(SkyComponent, Name) == nullptr)
			{
				return Name;
			}
		}
		return HazeColor(SkyComponent) == nullptr ? HazeMieAbsorptionColor : nullptr;
	}

	// The look on a sky whose properties are all there (MissingHazeProperty).
	terrain::unreal::HazeLook ReadHazeLook(USkyAtmosphereComponent& SkyComponent)
	{
		terrain::unreal::HazeLook Look;
		Look.viewDistanceScale =
			HazeFloat(SkyComponent, HazeViewDistance)->GetPropertyValue_InContainer(&SkyComponent);
		Look.mieScatteringScale =
			HazeFloat(SkyComponent, HazeMieScattering)->GetPropertyValue_InContainer(&SkyComponent);
		Look.mieAbsorptionScale =
			HazeFloat(SkyComponent, HazeMieAbsorption)->GetPropertyValue_InContainer(&SkyComponent);
		const FLinearColor& Color = *HazeColor(SkyComponent);
		Look.mieAbsorption = {Color.R, Color.G, Color.B};
		return Look;
	}
} // namespace

void FTerrainAtmosphereHaze::Start(USkyAtmosphereComponent* InSky,
	std::shared_ptr<const terrain::PlanetLevel> InPlanet, const FTerrainHorizonDrawing& Settings)
{
	Sky = InSky;
	Planet = std::move(InPlanet);
	DustAir.viewDistanceScale = Settings.DustViewDistanceScale;
	DustAir.mieScattering = Settings.DustMieScattering;
	DustAir.mieAbsorptionScale = Settings.DustMieAbsorptionScale;
	bOn = Settings.bDustHaze;
	Forced.reset();
	Follower.reset();
	SinceRefresh = 1.0e9;
	Applied = -1.0;
	bReady = false;
	if (InSky == nullptr)
	{
		UE_LOG(LogTerrainEngine, Log, TEXT("Terrain World: no haze of dust: no SkyAtmosphere"));
		return;
	}
	if (const TCHAR* Missing = MissingHazeProperty(*InSky))
	{
		UE_LOG(LogTerrainEngine, Warning,
			TEXT("Terrain World: no haze of dust: the SkyAtmosphere has no property %s"), Missing);
		return;
	}
	Map = ReadHazeLook(*InSky);
	bReady = true;
	UE_LOG(LogTerrainEngine, Log,
		TEXT("Terrain World: haze of dust over dry land %s%s; the map's SkyAtmosphere: aerial "
			 "perspective x %g, Mie scattering %g /km, Mie absorption %g /km; at a dryness of 1: "
			 "x %g, x %g, %g /km"),
		bOn ? TEXT("on") : TEXT("off"),
		Planet != nullptr ? TEXT("") : TEXT(" (no planet file: only Terrain.Haze <dryness>)"),
		Map.viewDistanceScale, Map.mieScatteringScale, Map.mieAbsorptionScale,
		DustAir.viewDistanceScale, DustAir.mieScattering, DustAir.mieAbsorptionScale);
}

void FTerrainAtmosphereHaze::Update(double CoreX, double CoreZ, double DeltaSeconds)
{
	if (!bReady || !bOn || Sky.Get() == nullptr)
	{
		return;
	}
	SinceRefresh += DeltaSeconds;
	if (SinceRefresh >= terrain::unreal::kHazeRefreshSeconds)
	{
		SinceRefresh = 0.0;
		Target = Forced ? *Forced
			: (Planet != nullptr ? terrain::unreal::regionDryness(*Planet, CoreX, CoreZ) : 0.0);
	}
	const double Shown = Follower.follow(Target, DeltaSeconds, CoreX, CoreZ);
	if (Applied < 0.0 || std::abs(Shown - Applied) > terrain::unreal::kHazeMinStep)
	{
		Apply(terrain::unreal::hazeLook(Map, DustAir, Shown));
		Applied = Shown;
	}
}

void FTerrainAtmosphereHaze::Stop()
{
	if (bReady && Sky.Get() != nullptr)
	{
		Apply(Map);
		UE_LOG(LogTerrainEngine, Log,
			TEXT("Terrain World: haze of dust stopped, the map's SkyAtmosphere set back"));
	}
	Applied = -1.0;
}

void FTerrainAtmosphereHaze::SetMode(bool bInOn, std::optional<double> InForced)
{
	bOn = bInOn;
	Forced = InForced;
	Follower.reset();
	SinceRefresh = 1.0e9;
	if (!bOn && bReady)
	{
		Apply(Map);
		Applied = -1.0;
	}
}

FString FTerrainAtmosphereHaze::Describe() const
{
	USkyAtmosphereComponent* SkyComponent = Sky.Get();
	if (!bReady || SkyComponent == nullptr)
	{
		return TEXT("no haze of dust (no SkyAtmosphere, or one of its properties missing)");
	}
	const terrain::unreal::HazeLook Now = ReadHazeLook(*SkyComponent);
	return FString::Printf(
		TEXT("%s, dryness shown %.3f (target %.3f); on the sky: aerial perspective x %g, Mie "
			 "scattering %g /km, Mie absorption %g /km x (%.2f, %.2f, %.2f); the map's: x %g, %g "
			 "/km, %g /km"),
		!bOn ? TEXT("off") : (Forced ? TEXT("forced") : TEXT("auto")), Follower.shown(), Target,
		Now.viewDistanceScale, Now.mieScatteringScale, Now.mieAbsorptionScale,
		Now.mieAbsorption[0], Now.mieAbsorption[1], Now.mieAbsorption[2], Map.viewDistanceScale,
		Map.mieScatteringScale, Map.mieAbsorptionScale);
}

void FTerrainAtmosphereHaze::Apply(const terrain::unreal::HazeLook& Look)
{
	USkyAtmosphereComponent* SkyComponent = Sky.Get();
	if (SkyComponent == nullptr || MissingHazeProperty(*SkyComponent) != nullptr)
	{
		return;
	}
	HazeFloat(*SkyComponent, HazeViewDistance)
		->SetPropertyValue_InContainer(SkyComponent, static_cast<float>(Look.viewDistanceScale));
	HazeFloat(*SkyComponent, HazeMieScattering)
		->SetPropertyValue_InContainer(SkyComponent, static_cast<float>(Look.mieScatteringScale));
	HazeFloat(*SkyComponent, HazeMieAbsorption)
		->SetPropertyValue_InContainer(SkyComponent, static_cast<float>(Look.mieAbsorptionScale));
	FLinearColor& Color = *HazeColor(*SkyComponent);
	Color = FLinearColor(static_cast<float>(Look.mieAbsorption[0]),
		static_cast<float>(Look.mieAbsorption[1]), static_cast<float>(Look.mieAbsorption[2]),
		Color.A);
	SkyComponent->MarkRenderStateDirty();
}

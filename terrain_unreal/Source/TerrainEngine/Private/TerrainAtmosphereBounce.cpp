// The warm bounce of the ground in the shade (agent 40): see TerrainAtmosphereBounce.h and
// Bridge/ground_bounce.hpp.
#include "TerrainAtmosphereBounce.h"

#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "TerrainEngineLog.h"
#include "TerrainHorizonDrawing.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace
{
	const TCHAR* const BounceLowerBlack = TEXT("bLowerHemisphereIsBlack");
	const TCHAR* const BounceGroundAlbedo = TEXT("GroundAlbedo");

	FBoolProperty* BounceBlack(const USkyLightComponent& SkyLight)
	{
		return FindFProperty<FBoolProperty>(SkyLight.GetClass(), BounceLowerBlack);
	}

	// GroundAlbedo, an FColor (sRGB) in Unreal 5, or an FLinearColor: nullptr if neither.
	FStructProperty* BounceAlbedo(const USkyAtmosphereComponent& Atmosphere)
	{
		FStructProperty* Property =
			FindFProperty<FStructProperty>(Atmosphere.GetClass(), BounceGroundAlbedo);
		const bool bColor = Property != nullptr &&
			(Property->Struct == TBaseStructure<FColor>::Get() ||
				Property->Struct == TBaseStructure<FLinearColor>::Get());
		return bColor ? Property : nullptr;
	}

	// The albedo of the ground, linear.
	std::array<double, 3> ReadAlbedo(USkyAtmosphereComponent& Atmosphere)
	{
		FStructProperty* Property = BounceAlbedo(Atmosphere);
		const FLinearColor Color = Property->Struct == TBaseStructure<FColor>::Get()
			? FLinearColor(*Property->ContainerPtrToValuePtr<FColor>(&Atmosphere))
			: *Property->ContainerPtrToValuePtr<FLinearColor>(&Atmosphere);
		return {Color.R, Color.G, Color.B};
	}

	void WriteAlbedo(USkyAtmosphereComponent& Atmosphere, const std::array<double, 3>& Albedo)
	{
		FStructProperty* Property = BounceAlbedo(Atmosphere);
		const FLinearColor Color(static_cast<float>(Albedo[0]), static_cast<float>(Albedo[1]),
			static_cast<float>(Albedo[2]), 1.0f);
		if (Property->Struct == TBaseStructure<FColor>::Get())
		{
			*Property->ContainerPtrToValuePtr<FColor>(&Atmosphere) = Color.ToFColor(true);
		}
		else
		{
			*Property->ContainerPtrToValuePtr<FLinearColor>(&Atmosphere) = Color;
		}
	}
} // namespace

void FTerrainGroundBounce::Start(USkyLightComponent* InLight, USkyAtmosphereComponent* InSky,
	const FTerrainHorizonDrawing& Settings)
{
	Light = InLight;
	Sky = InSky;
	WarmAlbedo = {Settings.BounceGroundAlbedo.R, Settings.BounceGroundAlbedo.G,
		Settings.BounceGroundAlbedo.B};
	bReady = false;
	bOnNow = false;
	if (InLight == nullptr || InSky == nullptr || BounceBlack(*InLight) == nullptr ||
		BounceAlbedo(*InSky) == nullptr)
	{
		UE_LOG(LogTerrainEngine, Log,
			TEXT("Terrain World: no warm bounce of the ground: %s"),
			InLight == nullptr ? TEXT("no SkyLight")
			: InSky == nullptr ? TEXT("no SkyAtmosphere")
							   : TEXT("the SkyLight or the SkyAtmosphere lacks its property"));
		return;
	}
	Map.lowerHemisphereBlack = BounceBlack(*InLight)->GetPropertyValue_InContainer(InLight);
	Map.groundAlbedo = ReadAlbedo(*InSky);
	MapAlbedoRaw = BounceAlbedo(*InSky)->Struct == TBaseStructure<FColor>::Get()
		? TOptional<FColor>(*BounceAlbedo(*InSky)->ContainerPtrToValuePtr<FColor>(InSky))
		: TOptional<FColor>();
	bReady = true;
	Set(Settings.bGroundBounce, WarmAlbedo);
	UE_LOG(LogTerrainEngine, Log, TEXT("Terrain World: warm bounce of the ground %s; %s"),
		Settings.bGroundBounce ? TEXT("on") : TEXT("off"), *Describe());
}

void FTerrainGroundBounce::Set(bool bOn, const std::array<double, 3>& Albedo)
{
	if (!bReady)
	{
		return;
	}
	WarmAlbedo = Albedo;
	bOnNow = bOn;
	Apply(terrain::unreal::bounceLook(Map, bOn, Albedo));
}

void FTerrainGroundBounce::Stop()
{
	if (bReady && bOnNow)
	{
		Apply(Map);
		UE_LOG(LogTerrainEngine, Log,
			TEXT("Terrain World: warm bounce stopped, the map's SkyLight and SkyAtmosphere set "
				 "back"));
	}
	bOnNow = false;
}

FString FTerrainGroundBounce::Describe() const
{
	USkyLightComponent* SkyLight = Light.Get();
	USkyAtmosphereComponent* Atmosphere = Sky.Get();
	if (!bReady || SkyLight == nullptr || Atmosphere == nullptr)
	{
		return TEXT("no warm bounce (no SkyLight or SkyAtmosphere, or a property missing)");
	}
	const std::array<double, 3> Albedo = ReadAlbedo(*Atmosphere);
	return FString::Printf(
		TEXT("%s: lower hemisphere of the SkyLight %s, ground of the SkyAtmosphere (%.3f, %.3f, "
			 "%.3f) linear (the map's: %s, (%.3f, %.3f, %.3f))"),
		bOnNow ? TEXT("on") : TEXT("off"),
		BounceBlack(*SkyLight)->GetPropertyValue_InContainer(SkyLight) ? TEXT("black")
																	   : TEXT("from its capture"),
		Albedo[0], Albedo[1], Albedo[2],
		Map.lowerHemisphereBlack ? TEXT("black") : TEXT("captured"),
		Map.groundAlbedo[0], Map.groundAlbedo[1], Map.groundAlbedo[2]);
}

void FTerrainGroundBounce::Apply(const terrain::unreal::BounceLook& Look)
{
	USkyLightComponent* SkyLight = Light.Get();
	USkyAtmosphereComponent* Atmosphere = Sky.Get();
	if (SkyLight == nullptr || Atmosphere == nullptr)
	{
		return;
	}
	BounceBlack(*SkyLight)->SetPropertyValue_InContainer(SkyLight, Look.lowerHemisphereBlack);
	if (Look == Map && MapAlbedoRaw.IsSet())
	{
		// The map's color itself, not its linear value turned back to sRGB.
		*BounceAlbedo(*Atmosphere)->ContainerPtrToValuePtr<FColor>(Atmosphere) =
			MapAlbedoRaw.GetValue();
	}
	else
	{
		WriteAlbedo(*Atmosphere, Look.groundAlbedo);
	}
	SkyLight->MarkRenderStateDirty();
	if (!SkyLight->IsRealTimeCaptureEnabled())
	{
		SkyLight->RecaptureSky(); // a captured SkyLight takes it at its next capture
	}
	Atmosphere->MarkRenderStateDirty();
}

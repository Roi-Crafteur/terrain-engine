// The sky over the far horizon of ATerrainWorld (phase 13): the SkyAtmosphere and the height fog
// of the level follow the camera over the curved planet (Bridge/horizon_atmosphere.hpp), and the
// aerial perspective reaches the end of the horizon.
#include "TerrainWorld.h"

#include "Bridge/horizon_atmosphere.hpp"
#include "Bridge/horizon_layout.hpp"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "TerrainAtmosphereFollower.h"
#include "TerrainEngineLog.h"
#include "TerrainHorizonClipmap.h"
#include "TerrainWorldState.h"

namespace
{
	// The depth of the camera volume of the aerial perspective, km (96 by default).
	const TCHAR* const AerialPerspectiveDepthVariable =
		TEXT("r.SkyAtmosphere.AerialPerspectiveLUT.Depth");

	template <typename TActor>
	TActor* FirstActor(UWorld* World)
	{
		if (World != nullptr)
		{
			for (TActorIterator<TActor> It(World); It; ++It)
			{
				return *It;
			}
		}
		return nullptr;
	}
} // namespace

void ATerrainWorld::StartAtmosphere()
{
	if (!bAtmosphereFollowsCamera)
	{
		return;
	}
	UWorld* World = GetWorld();
	ASkyAtmosphere* SkyActor =
		SkyAtmosphere != nullptr ? SkyAtmosphere.Get() : FirstActor<ASkyAtmosphere>(World);
	AExponentialHeightFog* FogActor =
		HeightFog != nullptr ? HeightFog.Get() : FirstActor<AExponentialHeightFog>(World);
	ASkyLight* LightActor = SkyLight != nullptr ? SkyLight.Get() : FirstActor<ASkyLight>(World);
	USkyAtmosphereComponent* Sky = SkyActor != nullptr ? SkyActor->GetComponent() : nullptr;
	UExponentialHeightFogComponent* Fog = FogActor != nullptr ? FogActor->GetComponent() : nullptr;
	// A SkyLight captured once keeps its sky wherever it stands: only a real-time capture moves.
	USkyLightComponent* Light = LightActor != nullptr ? LightActor->GetLightComponent() : nullptr;
	if (Light != nullptr && !Light->IsRealTimeCaptureEnabled())
	{
		Light = nullptr;
	}
	if (Sky == nullptr && Fog == nullptr && Light == nullptr)
	{
		UE_LOG(LogTerrainEngine, Log,
			TEXT("Terrain World: no SkyAtmosphere, ExponentialHeightFog or SkyLight in the level: "
				 "no sky follows the camera"));
		return;
	}
	const double RadiusKm = PlanetRadiusMeters() / 1000.0;
	if (Sky != nullptr)
	{
		// The planet of the horizon: its top at the component, put at the sea level under the
		// camera every frame.
		Sky->TransformMode = ESkyAtmosphereTransformMode::PlanetTopAtComponentTransform;
		Sky->SetBottomRadius(static_cast<float>(RadiusKm));
		Sky->MarkRenderStateDirty();
	}
	const double SeaLevelZ = terrain::unreal::seaLevelZ(State->Frame);
	const double FogAboveSeaCm = Fog != nullptr ? Fog->GetComponentLocation().Z - SeaLevelZ : 0.0;
	if (Fog != nullptr && FogEndDistanceKm > 0.0f)
	{
		Fog->SetEndDistance(FogEndDistanceKm * 1.0e5f);
	}
	const double DepthKm = ExtendAerialPerspective();
	AtmosphereFollower = NewObject<UTerrainAtmosphereFollower>(this);
	AtmosphereFollower->SetFrame(State->Frame);
	AtmosphereFollower->Follow(Sky, Fog, Light, FogAboveSeaCm);
	AtmosphereFollower->RegisterComponent();
	UE_LOG(LogTerrainEngine, Log,
		TEXT("Terrain World: the sky follows the camera: %s on a planet of %.0f km, its top at the "
			 "sea level under the camera; %s %.0f m above the sea level, integrated over %.0f km; "
			 "%s on the camera; aerial perspective over %.0f km"),
		Sky != nullptr ? *SkyActor->GetName() : TEXT("no SkyAtmosphere"), RadiusKm,
		Fog != nullptr ? *FogActor->GetName() : TEXT("no height fog"), FogAboveSeaCm / 100.0,
		Fog != nullptr ? Fog->EndDistance / 1.0e5 : 0.0,
		Light != nullptr ? *LightActor->GetName() : TEXT("no SkyLight"), DepthKm);
}

void ATerrainWorld::StopAtmosphere()
{
	if (AtmosphereFollower != nullptr)
	{
		AtmosphereFollower->DestroyComponent();
		AtmosphereFollower = nullptr;
	}
	if (RestoredAerialPerspectiveDepthKm >= 0.0f)
	{
		if (IConsoleVariable* Depth =
				IConsoleManager::Get().FindConsoleVariable(AerialPerspectiveDepthVariable))
		{
			Depth->Set(RestoredAerialPerspectiveDepthKm, ECVF_SetByCode);
		}
		RestoredAerialPerspectiveDepthKm = -1.0f;
	}
}

// The camera volume of the aerial perspective reaches the farthest point of the horizon, or the
// far mountains would stay as clear as at its 96 km. Returns its depth, km.
double ATerrainWorld::ExtendAerialPerspective()
{
	IConsoleVariable* Depth =
		IConsoleManager::Get().FindConsoleVariable(AerialPerspectiveDepthVariable);
	if (Depth == nullptr)
	{
		return 0.0;
	}
	const terrain::HorizonParams* Params = State->HorizonGpu != nullptr
		? &State->HorizonGpu->params()
		: (State->Horizon != nullptr ? &State->Horizon->params() : nullptr);
	if (Params != nullptr)
	{
		const float Needed = static_cast<float>(terrain::unreal::aerialPerspectiveDepthKm(*Params));
		if (Depth->GetFloat() < Needed)
		{
			RestoredAerialPerspectiveDepthKm = Depth->GetFloat();
			Depth->Set(Needed, ECVF_SetByCode);
		}
	}
	return Depth->GetFloat();
}

// The radius of the curvature of the horizon, which the sky shares.
double ATerrainWorld::PlanetRadiusMeters() const
{
	if (State->HorizonGpu != nullptr)
	{
		return State->HorizonGpu->planetRadiusMeters();
	}
	if (State->Horizon != nullptr)
	{
		return State->Horizon->planetRadiusMeters();
	}
	return PlanetRadiusKm > 0.0
		? PlanetRadiusKm * 1000.0
		: terrain::unreal::planetRadiusMeters(State->Session.streamer().manager().params().world);
}

// The frame moved (origin rebasing): the sea level and the origin of the waves follow it.
void ATerrainWorld::FollowFrame()
{
	if (AtmosphereFollower != nullptr)
	{
		AtmosphereFollower->SetFrame(State->Frame);
	}
	if (HorizonClipmap != nullptr)
	{
		HorizonClipmap->SetWaveOrigin(terrain::unreal::seaWaveOrigin(State->Frame));
	}
}

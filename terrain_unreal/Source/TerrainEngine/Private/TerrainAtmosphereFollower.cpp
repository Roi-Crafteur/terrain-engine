#include "TerrainAtmosphereFollower.h"

#include "Bridge/horizon_atmosphere.hpp"
#include "Camera/PlayerCameraManager.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	// Moves a component only when it must: an unchanged transform sends nothing to the renderer.
	void MoveTo(USceneComponent& Component, const FVector& Location)
	{
		if (!Component.GetComponentLocation().Equals(Location, 0.0))
		{
			Component.SetWorldLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
} // namespace

UTerrainAtmosphereFollower::UTerrainAtmosphereFollower()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// After the update of the cameras, before the frame is drawn.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UTerrainAtmosphereFollower::Follow(USkyAtmosphereComponent* InSky,
	UExponentialHeightFogComponent* InFog, USkyLightComponent* InLight, double InFogAboveSeaCm)
{
	Sky = InSky;
	Fog = InFog;
	Light = InLight;
	FogAboveSeaCm = InFogAboveSeaCm;
	for (USceneComponent* Component : {static_cast<USceneComponent*>(InSky),
			 static_cast<USceneComponent*>(InFog), static_cast<USceneComponent*>(InLight)})
	{
		if (Component != nullptr && Component->Mobility != EComponentMobility::Movable)
		{
			Component->SetMobility(EComponentMobility::Movable);
		}
	}
}

void UTerrainAtmosphereFollower::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	FVector Camera;
	if (FindCamera(Camera))
	{
		PlaceFor(Camera);
	}
}

void UTerrainAtmosphereFollower::PlaceFor(const FVector& Camera)
{
	const terrain::unreal::UnrealPoint Top =
		terrain::unreal::planetTopUnder({Camera.X, Camera.Y, Camera.Z}, Frame);
	if (USkyAtmosphereComponent* Atmosphere = Sky.Get())
	{
		MoveTo(*Atmosphere, FVector(Top.x, Top.y, Top.z));
	}
	if (UExponentialHeightFogComponent* HeightFog = Fog.Get())
	{
		MoveTo(*HeightFog, FVector(Top.x, Top.y, Top.z + FogAboveSeaCm));
	}
	if (USkyLightComponent* SkyLight = Light.Get())
	{
		MoveTo(*SkyLight, Camera);
	}
}

// The camera of the first local player, as updated this frame.
bool UTerrainAtmosphereFollower::FindCamera(FVector& OutLocation) const
{
	const APlayerCameraManager* Manager = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (Manager == nullptr)
	{
		return false;
	}
	OutLocation = Manager->GetCameraLocation();
	return true;
}

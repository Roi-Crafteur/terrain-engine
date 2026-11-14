// The shadows of the GPU horizon under a low sun (agent 40, FTerrainHorizonDrawing::
// LowSunShadowDegrees, Bridge/horizon_low_sun.hpp): its levels 0 to 3 cast them while the sun of
// the atmosphere is low, so that the long shadows of the relief go on beyond the tiles over the far
// plain; none under a higher sun, where they show nothing and only cost the shadow passes. The
// coarser levels never cast them (bHorizonShadows aside). The console command
// Terrain.HorizonShadows of the tools forces them on or off while playing (TerrainDevLight.cpp).
#include "TerrainHorizonClipmap.h"

#include "Bridge/horizon_low_sun.hpp"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "TerrainEngineLog.h"

namespace
{
	// The light of the sun of the atmosphere: the first DirectionalLight that lights the
	// SkyAtmosphere as its sun, or else the first one.
	UDirectionalLightComponent* FindAtmosphereSun(UWorld& World)
	{
		UDirectionalLightComponent* First = nullptr;
		for (TActorIterator<ADirectionalLight> It(&World); It; ++It)
		{
			UDirectionalLightComponent* Light =
				Cast<UDirectionalLightComponent>(It->GetLightComponent());
			if (Light == nullptr)
			{
				continue;
			}
			if (Light->IsUsedAsAtmosphereSunLight() && Light->GetAtmosphereSunLightIndex() == 0)
			{
				return Light;
			}
			First = First != nullptr ? First : Light;
		}
		return First;
	}
} // namespace

void UTerrainHorizonClipmap::SetShadowLevels(int32 Levels)
{
	ShadowLevels = Levels;
	for (int32 Level = 0; Level < Bodies.Num(); ++Level)
	{
		const bool bCast = HorizonLook.bShadows || Level < Levels;
		if (UStaticMeshComponent* Body = Bodies[Level].Get())
		{
			Body->SetCastShadow(bCast);
		}
		if (Trims.IsValidIndex(Level))
		{
			if (UStaticMeshComponent* Trim = Trims[Level].Get())
			{
				Trim->SetCastShadow(bCast);
			}
		}
	}
}

void UTerrainHorizonClipmap::FollowSun()
{
	UDirectionalLightComponent* Light = Sun.Get();
	UWorld* World = OwnerActor != nullptr ? OwnerActor->GetWorld() : nullptr;
	const double Now = FPlatformTime::Seconds();
	if (Light == nullptr && World != nullptr && Now >= NextSunSearchSeconds)
	{
		NextSunSearchSeconds = Now + 1.0;
		Light = FindAtmosphereSun(*World);
		Sun = Light;
	}
	// Without a sun, as under a high one.
	const FVector Direction =
		Light != nullptr ? Light->GetForwardVector() : FVector(0.0, 0.0, -1.0);
	SunElevation = terrain::unreal::sunElevationDegrees(Direction.X, Direction.Y, Direction.Z);
	const int32 Wanted = ShadowOverride >= 0
		? ShadowOverride
		: terrain::unreal::lowSunShadowLevels(SunElevation, LowSunDegrees, ShadowLevels);
	if (Wanted == ShadowLevels)
	{
		return;
	}
	SetShadowLevels(Wanted);
	const FString Why = ShadowOverride >= 0
		? FString(TEXT("forced by Terrain.HorizonShadows"))
		: FString::Printf(TEXT("on under %.1f degrees, off over %.1f"), LowSunDegrees,
			  LowSunDegrees + terrain::unreal::kLowSunHysteresisDegrees);
	UE_LOG(LogTerrainEngine, Log,
		TEXT("Terrain World: horizon shadows %s: the sun %.1f degrees high (%s)"),
		Wanted > 0 ? *FString::Printf(TEXT("on for the levels 0 to %d"), Wanted - 1) : TEXT("off"),
		SunElevation, *Why);
}

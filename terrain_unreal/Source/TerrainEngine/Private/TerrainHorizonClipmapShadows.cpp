// The shadows of the GPU horizon under a low sun (agent 40, FTerrainHorizonDrawing::
// LowSunShadowDegrees, Bridge/horizon_low_sun.hpp): its levels 0 to 3 cast them while the sun of
// the atmosphere is low, so that the long shadows of the relief go on beyond the tiles over the far
// plain; none under a higher sun, where they show nothing and only cost the shadow passes. The
// coarser levels never cast them (bHorizonShadows aside). The console command
// Terrain.HorizonShadows of the tools forces them on or off while playing (TerrainDevHorizon.cpp).
// Their cache (session 2: 1.8 ms at 7 degrees on giants_B): Rigid on the levels that cast them,
// their pages of shadow redrawn only when a level moves or its texels arrive
// (Bridge/horizon_low_sun.hpp: shadowCacheOf, shadowCacheStale), set by the name of its property.
#include "TerrainHorizonClipmap.h"

#include "Bridge/horizon_low_sun.hpp"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "TerrainEngineLog.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

#include <vector>

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

	// The value of EShadowCacheInvalidationBehavior named Name, or INDEX_NONE.
	int64 ShadowCacheValue(const UEnum* Enum, const TCHAR* Name)
	{
		if (Enum == nullptr)
		{
			return INDEX_NONE;
		}
		const int64 Value = Enum->GetValueByNameString(Name);
		return Value != INDEX_NONE ? Value
								   : Enum->GetValueByNameString(Enum->GenerateFullEnumName(Name));
	}

	// ShadowCacheInvalidationBehavior of a component set by name ("Rigid", "Auto"), its render
	// state rebuilt only when it changes; false when this version of Unreal has no such property.
	bool SetShadowCache(UPrimitiveComponent& Component, const TCHAR* Name)
	{
		FProperty* Property = FindFProperty<FProperty>(UPrimitiveComponent::StaticClass(),
			TEXT("ShadowCacheInvalidationBehavior"));
		void* At =
			Property != nullptr ? Property->ContainerPtrToValuePtr<void>(&Component) : nullptr;
		int64 Value = INDEX_NONE;
		bool bChanged = false;
		if (FEnumProperty* AsEnum = CastField<FEnumProperty>(Property))
		{
			Value = ShadowCacheValue(AsEnum->GetEnum(), Name);
			FNumericProperty* Underlying = AsEnum->GetUnderlyingProperty();
			bChanged = Value != INDEX_NONE && Underlying->GetSignedIntPropertyValue(At) != Value;
			if (bChanged)
			{
				Underlying->SetIntPropertyValue(At, Value);
			}
		}
		else if (FByteProperty* AsByte = CastField<FByteProperty>(Property))
		{
			Value = ShadowCacheValue(AsByte->Enum, Name);
			bChanged = Value != INDEX_NONE && AsByte->GetPropertyValue(At) != Value;
			if (bChanged)
			{
				AsByte->SetPropertyValue(At, static_cast<uint8>(Value));
			}
		}
		if (bChanged)
		{
			Component.MarkRenderStateDirty();
		}
		return Value != INDEX_NONE;
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
	ApplyShadowCache();
}

void UTerrainHorizonClipmap::SetRigidShadowCache(bool bRigid)
{
	bRigidShadowCache = bRigid;
	ApplyShadowCache();
}

// The cache of the shadows of each level (shadowCacheOf), on its body and its trim.
void UTerrainHorizonClipmap::ApplyShadowCache()
{
	bool bMissing = false;
	for (int32 Level = 0; Level < Bodies.Num(); ++Level)
	{
		const bool bRigid = terrain::unreal::shadowCacheOf(Level, ShadowLevels,
			HorizonLook.bShadows, bRigidShadowCache) == terrain::unreal::ShadowCache::Rigid;
		UStaticMeshComponent* Trim = Trims.IsValidIndex(Level) ? Trims[Level].Get() : nullptr;
		for (UStaticMeshComponent* Component : {Bodies[Level].Get(), Trim})
		{
			const TCHAR* Cache = bRigid ? TEXT("Rigid") : TEXT("Auto");
			if (Component != nullptr && !SetShadowCache(*Component, Cache))
			{
				bMissing = true;
			}
		}
	}
	if (bMissing && !bShadowCacheWarned)
	{
		bShadowCacheWarned = true;
		UE_LOG(LogTerrainEngine, Warning,
			TEXT("Terrain World: no ShadowCacheInvalidationBehavior (Rigid, Auto) on the "
				 "components in this version of Unreal: the shadows of the horizon are redrawn "
				 "every frame"));
	}
}

// The levels whose shape the texels of this update changed while their components stay: their
// cached shadows redrawn (Rigid keeps them otherwise).
void UTerrainHorizonClipmap::InvalidateShadowCache(const terrain::unreal::HorizonGpuUpdate& Update)
{
	if (Update.regions.empty() || !bRigidShadowCache)
	{
		return;
	}
	std::vector<int> Uploaded;
	for (const terrain::unreal::TexelRegion& Region : Update.regions)
	{
		Uploaded.push_back(Region.level);
	}
	const uint32 Stale = terrain::unreal::shadowCacheStale(Uploaded, Bodies.Num(), ShadowLevels,
		HorizonLook.bShadows, bRigidShadowCache);
	for (int32 Level = 0; Level < Bodies.Num(); ++Level)
	{
		if ((Stale & (1u << Level)) == 0)
		{
			continue;
		}
		Bodies[Level]->MarkRenderStateDirty();
		if (Trims.IsValidIndex(Level) && Trims[Level] != nullptr)
		{
			Trims[Level]->MarkRenderStateDirty();
		}
	}
}

FString UTerrainHorizonClipmap::DescribeShadowCache() const
{
	IConsoleVariable* Enable =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.Virtual.Enable"));
	const int32 Virtual = Enable != nullptr ? Enable->GetInt() : -1;
	return FString::Printf(TEXT("cache %s; virtual shadow maps %s (r.Shadow.Virtual.Enable %d)"),
		bRigidShadowCache ? TEXT("Rigid") : TEXT("Auto"),
		Virtual > 0 ? TEXT("on") : TEXT("off: the cache Rigid keeps nothing"), Virtual);
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
		TEXT("Terrain World: horizon shadows %s: the sun %.1f degrees high (%s); %s"),
		Wanted > 0 ? *FString::Printf(TEXT("on for the levels 0 to %d"), Wanted - 1) : TEXT("off"),
		SunElevation, *Why, *DescribeShadowCache());
}

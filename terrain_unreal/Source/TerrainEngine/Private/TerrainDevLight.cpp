// The console commands of the light of the tools of the developers (agent 40), never in a Shipping
// build. They change the world only while playing (PIE or a game): the map of the editor is never
// touched, and nothing they set is saved.
// - Terrain.Sun <elevation> <heading>: the sun that many degrees above the horizon, at that heading
//   (degrees clockwise from the north, as the places): every DirectionalLight of the world turned
//   to FRotator(-elevation, heading + 90, 0), as the launcher of the checks turns them
//   (scripts/verify/ue_api.py, set_sun: its --sun is elevation,yaw with yaw = heading + 90); a
//   light that is not movable is made movable first, for this play only (SetMobility, logged);
//   without arguments, the sun of the first one.
// - Terrain.HorizonShadows <levels>: the levels of the GPU horizon that cast shadows
//   (FTerrainHorizonDrawing::ShadowLevels, 0 to 4), to compare their look and their cost.
// - Terrain.Fog [<density> [end km] [height falloff] [cutoff km]]: the ExponentialHeightFog of the
//   world, set by the setters of its component and its render state marked dirty, to find what
//   takes the blue band off the far ground seen from high up without losing the haze of the views
//   from the ground (points 6 and 7 of the orchestrator). Every height fog of the world is logged
//   with its values read back (a second fog shows there: the renderer draws only one), its second
//   fog and its volumetric fog.
// - Terrain.Aerial [<view distance scale> [height fog contribution]]: the aerial perspective of
//   the SkyAtmosphere (AerialPespectiveViewDistanceScale, HeightFogContribution), read and set by
//   the names of its properties (the log names one this version would not have), its render state
//   marked dirty: does the blue band come from it (ShowFlag.Atmosphere 0 takes 19.6/255 off)?
#include "TerrainHorizonClipmap.h"

#if TERRAIN_DEV_TOOLS

#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "TerrainEngineLog.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace
{
	// Degrees in [0, 360).
	double SunWrappedDegrees(double Degrees)
	{
		const double Turn = FMath::Fmod(Degrees, 360.0);
		return Turn < 0.0 ? Turn + 360.0 : Turn;
	}

	const TCHAR* SunMobilityName(EComponentMobility::Type Mobility)
	{
		switch (Mobility)
		{
		case EComponentMobility::Static:
			return TEXT("Static");
		case EComponentMobility::Stationary:
			return TEXT("Stationary");
		default:
			return TEXT("Movable");
		}
	}

	void LogTerrainSun(const ADirectionalLight& Light)
	{
		const FRotator Rotation = Light.GetActorRotation();
		UE_LOG(LogTerrainEngine, Log,
			TEXT("Terrain.Sun: %s, %.2f degrees high at heading %.2f (verify.py --sun %.2f,%.2f)"),
			*Light.GetName(), -Rotation.Pitch, SunWrappedDegrees(Rotation.Yaw - 90.0), -Rotation.Pitch,
			SunWrappedDegrees(Rotation.Yaw));
	}

	// False, logged, outside a game world: the map of the editor is never changed.
	bool LightChangesAllowed(const UWorld& World, const TCHAR* Command)
	{
		if (World.IsGameWorld())
		{
			return true;
		}
		UE_LOG(LogTerrainEngine, Warning,
			TEXT("%s: only while playing (PIE or a game): the map of the editor is left as it is"),
			Command);
		return false;
	}

	bool AllNumeric(const TArray<FString>& Args)
	{
		for (const FString& Arg : Args)
		{
			if (!Arg.IsNumeric())
			{
				return false;
			}
		}
		return true;
	}

	// Terrain.Sun [<elevation> <heading>]
	void RunTerrainSun(const TArray<FString>& Args, UWorld* World)
	{
		if (World == nullptr)
		{
			return;
		}
		if (Args.Num() == 0)
		{
			TActorIterator<ADirectionalLight> First(World);
			if (First)
			{
				LogTerrainSun(**First);
			}
			else
			{
				UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain.Sun: no DirectionalLight in the world"));
			}
			return;
		}
		if (Args.Num() < 2 || !Args[0].IsNumeric() || !Args[1].IsNumeric())
		{
			UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain.Sun <elevation> <heading>"));
			return;
		}
		if (!LightChangesAllowed(*World, TEXT("Terrain.Sun")))
		{
			return;
		}
		const double Elevation = FMath::Clamp(FCString::Atod(*Args[0]), -90.0, 90.0);
		const double Heading = SunWrappedDegrees(FCString::Atod(*Args[1]));
		const FRotator Rotation(-Elevation, SunWrappedDegrees(Heading + 90.0), 0.0);
		int32 Turned = 0;
		int32 MadeMovable = 0;
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			USceneComponent* Root = It->GetRootComponent();
			if (Root == nullptr)
			{
				continue;
			}
			const EComponentMobility::Type Before = Root->Mobility;
			if (Before != EComponentMobility::Movable)
			{
				Root->SetMobility(EComponentMobility::Movable);
				++MadeMovable;
				UE_LOG(LogTerrainEngine, Log,
					TEXT("Terrain.Sun: %s made Movable for this play (it was %s; the map is not "
						 "changed)"),
					*It->GetName(), SunMobilityName(Before));
			}
			It->SetActorRotation(Rotation);
			LogTerrainSun(**It); // read back
			++Turned;
		}
		UE_LOG(LogTerrainEngine, Log,
			TEXT("Terrain.Sun %.2f %.2f: %d DirectionalLight turned (verify.py --sun %.2f,%.2f), %d of "
				 "them made Movable first"),
			Elevation, Heading, Turned, Elevation, Rotation.Yaw, MadeMovable);
	}

	// Terrain.HorizonShadows <levels>
	void RunTerrainHorizonShadows(const TArray<FString>& Args, UWorld* World)
	{
		if (World == nullptr || Args.Num() < 1 || !Args[0].IsNumeric())
		{
			UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain.HorizonShadows <levels, 0 to 4>"));
			return;
		}
		const int32 Levels = FMath::Clamp(FCString::Atoi(*Args[0]), 0, 4);
		for (TObjectIterator<UTerrainHorizonClipmap> It; It; ++It)
		{
			if (It->GetWorld() == World)
			{
				It->SetShadowLevels(Levels);
			}
		}
		UE_LOG(LogTerrainEngine, Log,
			TEXT("Terrain.HorizonShadows %d: the levels 0 to %d of the horizon cast shadows"), Levels,
			Levels - 1);
	}

	// The values of a height fog as its component holds them.
	void LogTerrainFog(const UExponentialHeightFogComponent& Fog)
	{
		const AActor* Owner = Fog.GetOwner();
		UE_LOG(LogTerrainEngine, Log,
			TEXT("Terrain.Fog: %s.%s (%s, %s): density %g, height falloff %g, max opacity %g, start "
				 "%.2f km, end %.1f km, cutoff %.1f km (0: none); second fog: density %g, height "
				 "falloff %g; volumetric fog %s"),
			Owner != nullptr ? *Owner->GetName() : TEXT("(no actor)"), *Fog.GetName(),
			Fog.IsRegistered() ? TEXT("registered") : TEXT("not registered"),
			Fog.IsVisible() ? TEXT("visible") : TEXT("hidden"), Fog.FogDensity, Fog.FogHeightFalloff,
			Fog.FogMaxOpacity, Fog.StartDistance / 1.0e5, Fog.EndDistance / 1.0e5,
			Fog.FogCutoffDistance / 1.0e5, Fog.SecondFogData.FogDensity,
			Fog.SecondFogData.FogHeightFalloff, Fog.bEnableVolumetricFog ? TEXT("on") : TEXT("off"));
	}

	// Terrain.Fog <density> [end km] [height falloff] [cutoff km], on one registered fog.
	void SetTerrainFog(UExponentialHeightFogComponent& Fog, const TArray<FString>& Args)
	{
		Fog.SetFogDensity(FCString::Atof(*Args[0]));
		if (Args.Num() > 1)
		{
			Fog.SetEndDistance(FCString::Atof(*Args[1]) * 1.0e5f);
		}
		if (Args.Num() > 2)
		{
			Fog.SetFogHeightFalloff(FCString::Atof(*Args[2]));
		}
		if (Args.Num() > 3)
		{
			Fog.SetFogCutoffDistance(FCString::Atof(*Args[3]) * 1.0e5f);
		}
		Fog.MarkRenderStateDirty();
	}

	// Terrain.Fog [<density> [end km] [height falloff] [cutoff km]]
	void RunTerrainFog(const TArray<FString>& Args, UWorld* World)
	{
		if (World == nullptr)
		{
			return;
		}
		if (!AllNumeric(Args))
		{
			UE_LOG(LogTerrainEngine, Warning,
				TEXT("Terrain.Fog [<density> [end km] [height falloff] [cutoff km]]"));
			return;
		}
		const bool bSet = Args.Num() > 0 && LightChangesAllowed(*World, TEXT("Terrain.Fog"));
		int32 Fogs = 0;
		int32 Changed = 0;
		for (TObjectIterator<UExponentialHeightFogComponent> It; It; ++It)
		{
			UExponentialHeightFogComponent* Fog = *It;
			if (Fog == nullptr || Fog->GetWorld() != World)
			{
				continue;
			}
			++Fogs;
			if (bSet && Fog->IsRegistered())
			{
				SetTerrainFog(*Fog, Args);
				++Changed;
			}
			LogTerrainFog(*Fog); // read back
		}
		UE_LOG(LogTerrainEngine, Log,
			TEXT("Terrain.Fog: %d height fog component(s) in the world (the renderer draws one), %d "
				 "set"),
			Fogs, Changed);
	}

	// Terrain.Aerial [<view distance scale> [height fog contribution]]
	void RunTerrainAerial(const TArray<FString>& Args, UWorld* World)
	{
		if (World == nullptr)
		{
			return;
		}
		if (!AllNumeric(Args))
		{
			UE_LOG(LogTerrainEngine, Warning,
				TEXT("Terrain.Aerial [<view distance scale> [height fog contribution]]"));
			return;
		}
		static const TCHAR* const Names[] = {
			TEXT("AerialPespectiveViewDistanceScale"), TEXT("HeightFogContribution")};
		const bool bSet = Args.Num() > 0 && LightChangesAllowed(*World, TEXT("Terrain.Aerial"));
		int32 Skies = 0;
		for (TObjectIterator<USkyAtmosphereComponent> It; It; ++It)
		{
			USkyAtmosphereComponent* Sky = *It;
			if (Sky == nullptr || Sky->GetWorld() != World)
			{
				continue;
			}
			++Skies;
			const bool bSetThis = bSet && Sky->IsRegistered();
			FString Values;
			for (int32 N = 0; N < static_cast<int32>(UE_ARRAY_COUNT(Names)); ++N)
			{
				FFloatProperty* Property = FindFProperty<FFloatProperty>(Sky->GetClass(), Names[N]);
				if (Property == nullptr)
				{
					Values += FString::Printf(TEXT(", %s: no such float property"), Names[N]);
					continue;
				}
				if (bSetThis && Args.Num() > N)
				{
					Property->SetPropertyValue_InContainer(Sky, FCString::Atof(*Args[N]));
				}
				Values += FString::Printf(
					TEXT(", %s %g"), Names[N], Property->GetPropertyValue_InContainer(Sky));
			}
			if (bSetThis)
			{
				Sky->MarkRenderStateDirty();
			}
			const AActor* Owner = Sky->GetOwner();
			UE_LOG(LogTerrainEngine, Log, TEXT("Terrain.Aerial: %s.%s (%s)%s"),
				Owner != nullptr ? *Owner->GetName() : TEXT("(no actor)"), *Sky->GetName(),
				Sky->IsRegistered() ? TEXT("registered") : TEXT("not registered"), *Values);
		}
		if (Skies == 0)
		{
			UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain.Aerial: no SkyAtmosphere in the world"));
		}
	}

	FAutoConsoleCommandWithWorldAndArgs TerrainSunCommand(TEXT("Terrain.Sun"),
		TEXT("Turns the sun while playing: Terrain.Sun <elevation> <heading> (degrees above the "
			 "horizon, degrees clockwise from the north), a light not movable made movable for the "
			 "play; without arguments, logs it"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunTerrainSun));

	FAutoConsoleCommandWithWorldAndArgs TerrainHorizonShadowsCommand(
		TEXT("Terrain.HorizonShadows"),
		TEXT("The levels of the far horizon that cast shadows, finest first: "
			 "Terrain.HorizonShadows <0 to 4>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunTerrainHorizonShadows));

	FAutoConsoleCommandWithWorldAndArgs TerrainFogCommand(TEXT("Terrain.Fog"),
		TEXT("The height fog of the world while playing: Terrain.Fog <density> [end km] [height "
			 "falloff] [cutoff km]; logs the values read back (without arguments, only logs them)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunTerrainFog));

	FAutoConsoleCommandWithWorldAndArgs TerrainAerialCommand(TEXT("Terrain.Aerial"),
		TEXT("The aerial perspective of the SkyAtmosphere while playing: Terrain.Aerial <view "
			 "distance scale> [height fog contribution]; logs the values read back (without "
			 "arguments, only logs them)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunTerrainAerial));
} // namespace

#endif // TERRAIN_DEV_TOOLS

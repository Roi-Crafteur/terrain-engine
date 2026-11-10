// The console commands of the light of the tools of the developers (agent 40), never in a Shipping
// build:
// - Terrain.Sun <elevation> <heading>: the sun that many degrees above the horizon, at that heading
//   (degrees clockwise from the north, as the places): every DirectionalLight of the world turned
//   to FRotator(-elevation, heading + 90, 0), as the launcher of the checks turns them
//   (scripts/verify/ue_api.py, set_sun: its --sun is elevation,yaw with yaw = heading + 90);
//   without arguments, the sun of the first one. A light that is not movable is left as it is.
// - Terrain.HorizonShadows <levels>: the levels of the GPU horizon that cast shadows
//   (FTerrainHorizonDrawing::ShadowLevels, 0 to 4), to compare their look and their cost.
#include "TerrainHorizonClipmap.h"

#if TERRAIN_DEV_TOOLS

#include "Components/SceneComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "TerrainEngineLog.h"
#include "UObject/UObjectIterator.h"

namespace
{
	// Degrees in [0, 360).
	double SunWrappedDegrees(double Degrees)
	{
		const double Turn = FMath::Fmod(Degrees, 360.0);
		return Turn < 0.0 ? Turn + 360.0 : Turn;
	}

	void LogTerrainSun(const ADirectionalLight& Light)
	{
		const FRotator Rotation = Light.GetActorRotation();
		UE_LOG(LogTerrainEngine, Log,
			TEXT("Terrain.Sun: %s, %.2f degrees high at heading %.2f (verify.py --sun %.2f,%.2f)"),
			*Light.GetName(), -Rotation.Pitch, SunWrappedDegrees(Rotation.Yaw - 90.0), -Rotation.Pitch,
			SunWrappedDegrees(Rotation.Yaw));
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
		const double Elevation = FMath::Clamp(FCString::Atod(*Args[0]), -90.0, 90.0);
		const double Heading = SunWrappedDegrees(FCString::Atod(*Args[1]));
		const FRotator Rotation(-Elevation, SunWrappedDegrees(Heading + 90.0), 0.0);
		int32 Turned = 0;
		int32 Fixed = 0;
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			const USceneComponent* Root = It->GetRootComponent();
			if (Root == nullptr || Root->Mobility != EComponentMobility::Movable)
			{
				++Fixed;
				continue;
			}
			It->SetActorRotation(Rotation);
			++Turned;
		}
		UE_LOG(LogTerrainEngine, Log,
			TEXT("Terrain.Sun %.2f %.2f: %d DirectionalLight turned (verify.py --sun %.2f,%.2f), %d "
				 "left as they are (not movable)"),
			Elevation, Heading, Turned, Elevation, Rotation.Yaw, Fixed);
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

	FAutoConsoleCommandWithWorldAndArgs TerrainSunCommand(TEXT("Terrain.Sun"),
		TEXT("Turns the sun: Terrain.Sun <elevation> <heading> (degrees above the horizon, degrees "
			 "clockwise from the north); without arguments, logs it"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunTerrainSun));

	FAutoConsoleCommandWithWorldAndArgs TerrainHorizonShadowsCommand(
		TEXT("Terrain.HorizonShadows"),
		TEXT("The levels of the far horizon that cast shadows, finest first: "
			 "Terrain.HorizonShadows <0 to 4>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunTerrainHorizonShadows));
} // namespace

#endif // TERRAIN_DEV_TOOLS

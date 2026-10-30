// The water of ATerrainWorld (agent 27): the materials of the water of the tiles (the surface of
// their lakes and rivers, the curtains and foam of their falls), the lakes and rivers of the GPU
// horizon, the console command Terrain.Water 0|1 that shows or hides them all to compare their
// cost, and the line of the log about the planet level (read from its file, or built and why).
#include "TerrainWorldWater.h"

#include "TerrainWorld.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "TerrainEngineLog.h"
#include "TerrainHorizonClipmap.h"
#include "TerrainMeshBackend.h"
#include "TerrainWorldState.h"

namespace
{
	using TerrainWorldConversions::ToFString;

	// Created by terrain_unreal/Scripts/create_water_materials.py.
	const TCHAR* const WaterMaterialPath = TEXT("/Game/TerrainEngine/M_TerrainWater.M_TerrainWater");
	const TCHAR* const FallsMaterialPath = TEXT("/Game/TerrainEngine/M_TerrainFalls.M_TerrainFalls");

	UMaterialInterface* Resolve(UMaterialInterface* Set, const TCHAR* Path, const TCHAR* What)
	{
		if (Set != nullptr)
		{
			return Set;
		}
		UMaterialInterface* Created =
			LoadObject<UMaterialInterface>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (Created == nullptr)
		{
			UE_LOG(LogTerrainEngine, Warning,
				TEXT("Terrain World: no %s material set and no %s: the tiles draw no %s (run "
					 "terrain_unreal/Scripts/create_water_materials.py)"),
				What, Path, What);
		}
		return Created;
	}

	double Megabytes(uint64 Bytes)
	{
		return static_cast<double>(Bytes) / (1024.0 * 1024.0);
	}

	// Terrain.Water 0|1: on every Terrain World of the world.
	void RunTerrainWater(const TArray<FString>& Args, UWorld* World)
	{
		if (World == nullptr || Args.Num() < 1)
		{
			UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain.Water 0|1"));
			return;
		}
		const bool bShown = FCString::Atoi(*Args[0]) != 0;
		for (TActorIterator<ATerrainWorld> It(World); It; ++It)
		{
			It->SetWaterShown(bShown);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs TerrainWaterCommand(TEXT("Terrain.Water"),
		TEXT("Shows (1) or hides (0) the lakes, rivers and falls of the tiles and the lakes and "
			 "rivers of the far horizon, to compare their cost: Terrain.Water 0|1"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunTerrainWater));
} // namespace

void LogPlanet(const terrain::unreal::PlanetSource& Source, const std::string& File,
	const FPlatformMemoryStats& Before)
{
	if (File.empty())
	{
		UE_LOG(LogTerrainEngine, Log,
			TEXT("Terrain World: no planet file: the relief alone, without rivers nor lakes"));
		return;
	}
	const FPlatformMemoryStats After = FPlatformMemory::GetStats();
	const FString Line = FString::Printf(
		TEXT("Terrain World: %s; process memory %.0f MB before, %.0f MB after, peak %.0f MB"),
		*ToFString(terrain::unreal::describe(Source, File)), Megabytes(Before.UsedPhysical),
		Megabytes(After.UsedPhysical), Megabytes(After.PeakUsedPhysical));
	if (Source.built)
	{
		UE_LOG(LogTerrainEngine, Warning, TEXT("%s"), *Line);
	}
	else
	{
		UE_LOG(LogTerrainEngine, Log, TEXT("%s"), *Line);
	}
}

void ATerrainWorld::StartWater()
{
	if (Backend == nullptr)
	{
		return;
	}
	if (bWater)
	{
		Backend->SetWaterMaterials(Resolve(WaterMaterial, WaterMaterialPath, TEXT("water")),
			Resolve(FallsMaterial, FallsMaterialPath, TEXT("falls")));
	}
	if (HorizonClipmap != nullptr)
	{
		HorizonClipmap->SetInlandWater(bWater);
	}
}

bool ATerrainWorld::SetWaterShown(bool bShown)
{
	if (!bWater || Backend == nullptr)
	{
		return false;
	}
	Backend->SetWaterVisible(bShown);
	if (HorizonClipmap != nullptr)
	{
		HorizonClipmap->SetInlandWater(bShown);
	}
	UE_LOG(LogTerrainEngine, Log, TEXT("Terrain World: water %s"),
		bShown ? TEXT("shown") : TEXT("hidden"));
	return true;
}

// The console commands of the far horizon of the tools of the developers (agent 40), never in a
// Shipping build. They change the world only while playing, and nothing they set is saved.
// - Terrain.HorizonShadows auto|off|on: the shadows of the levels 0 to 3 of the GPU horizon under
//   the rule of the low sun (FTerrainHorizonDrawing::LowSunShadowDegrees), or forced off or on,
//   to compare their look and their cost (Terrain.Cost).
// - Terrain.HorizonCenters <0 to 10>: the levels whose grids hold the centers of their cells
//   (FTerrainHorizonDrawing::HorizonCenterLevels, 4 by default; 10: every level, as the commit 13
//   of agent 40; 0: the grids before it), to measure what the centers cost: their vertices
//   (Terrain.Param AdaptiveSplit 0, then 0 against 4), the vertex shader (AdaptiveSplit 0 against
//   1), the shadows (the same with the horizon casting them).
#include "TerrainHorizonClipmap.h"

#if TERRAIN_DEV_TOOLS

#include "Bridge/dev_commands.hpp"
#include "Bridge/horizon_low_sun.hpp"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TerrainDevState.h"
#include "TerrainEngineLog.h"
#include "UObject/UObjectIterator.h"

#include <cmath>
#include <optional>

namespace
{
	// Terrain.HorizonShadows auto|off|on: the rule of the low sun, or the levels 0 to 3 forced off
	// or on (Bridge/horizon_low_sun.hpp).
	void RunTerrainHorizonShadows(const TArray<FString>& Args, UWorld* World)
	{
		const FString Mode = Args.Num() == 1 ? Args[0].ToLower() : FString();
		int32 Override = -2;
		if (Mode == TEXT("auto"))
		{
			Override = -1;
		}
		else if (Mode == TEXT("off") || Mode == TEXT("on"))
		{
			Override = Mode == TEXT("on") ? terrain::unreal::kLowSunShadowLevels : 0;
		}
		if (World == nullptr || Override == -2)
		{
			UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain.HorizonShadows auto|off|on"));
			return;
		}
		for (TObjectIterator<UTerrainHorizonClipmap> It; It; ++It)
		{
			if (It->GetWorld() == World)
			{
				It->SetShadowOverride(Override);
				It->FollowSun(); // at once, with its line of log
				UE_LOG(LogTerrainEngine, Log,
					TEXT("Terrain.HorizonShadows %s: %d levels cast shadows (the finest first; 0: "
						 "none), the sun %.1f degrees high"),
					*Mode, It->GetShadowLevels(), It->GetSunElevation());
			}
		}
	}

	// Terrain.HorizonCenters <0 to 10>; without argument, logs the limit.
	void RunTerrainHorizonCenters(const TArray<FString>& Args, UWorld* World)
	{
		const std::optional<double> Number = Args.Num() == 1
			? terrain::unreal::parseNumber(TerrainDev::ToUtf8(Args[0]))
			: std::nullopt;
		if (World == nullptr || Args.Num() > 1 || (Args.Num() == 1 && !Number))
		{
			UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain.HorizonCenters <0 to 10>"));
			return;
		}
		for (TObjectIterator<UTerrainHorizonClipmap> It; It; ++It)
		{
			if (It->GetWorld() != World)
			{
				continue;
			}
			if (Number)
			{
				It->SetCenterLevels(static_cast<int32>(std::lround(*Number)));
			}
			UE_LOG(LogTerrainEngine, Log,
				TEXT("Terrain.HorizonCenters: the grids of the levels below %d hold the centers of "
					 "their cells; the horizon draws %lld vertices and %lld triangles"),
				It->GetCenterLevels(), static_cast<long long>(It->GetVertexCount()),
				static_cast<long long>(It->GetTriangleCount()));
		}
	}

	FAutoConsoleCommandWithWorldAndArgs TerrainHorizonShadowsCommand(
		TEXT("Terrain.HorizonShadows"),
		TEXT("The shadows of the levels 0 to 3 of the far horizon: Terrain.HorizonShadows auto "
			 "(under a low sun, FTerrainHorizonDrawing::LowSunShadowDegrees), off or on"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunTerrainHorizonShadows));

	FAutoConsoleCommandWithWorldAndArgs TerrainHorizonCentersCommand(
		TEXT("Terrain.HorizonCenters"),
		TEXT("The levels of the far horizon whose grids hold the centers of their cells: "
			 "Terrain.HorizonCenters <0 to 10> (4 by default); without argument, logs it"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunTerrainHorizonCenters));
} // namespace

#endif // TERRAIN_DEV_TOOLS

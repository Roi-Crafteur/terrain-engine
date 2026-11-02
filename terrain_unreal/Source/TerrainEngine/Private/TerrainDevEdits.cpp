// Terrain.EditStats of the tools of the developers (agent 28): the time from a click of the player
// to the tile it changed shown again, over the last edits (Bridge/edit_latency.hpp), and the saves
// of the edits (Bridge/edit_save.hpp). Only with TERRAIN_DEV_TOOLS: never in a Shipping build.
#include "CoreMinimal.h"

#if TERRAIN_DEV_TOOLS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "TerrainEngineLog.h"
#include "TerrainWorld.h"
#include "TerrainWorldBuild.h"
#include "TerrainWorldState.h"

namespace
{
	// The summary of the edits of a world, for the log and the screen.
	FString EditStats(FTerrainWorldState& State)
	{
		const terrain::unreal::LatencySummary Latency = State.Build.Latency.summary();
		FString Text = FString::Printf(TEXT("%d edits of the player measured: click -> done %.1f ms "
			"(median), click -> tile shown %.1f ms median, %.1f ms p95, %.1f ms max, %lld frames "
			"median, %lld max; %d waiting"), static_cast<int32>(Latency.count),
			Latency.doneMedian, Latency.shownMedian, Latency.shownP95, Latency.shownMax,
			static_cast<long long>(Latency.framesMedian), static_cast<long long>(Latency.framesMax),
			static_cast<int32>(State.Build.Latency.waiting()));
		if (State.Build.Saver == nullptr)
		{
			Text += State.Build.bSaveRefused ? TEXT("; save refused at the start") : TEXT("; save off");
			return Text;
		}
		const terrain::unreal::EditSaver::Report Save = State.Build.Saver->report();
		Text += FString::Printf(TEXT("; %d saves, the last of %d regions in %.1f ms on its thread, "
			"%d regions to save"), static_cast<int32>(Save.saves), static_cast<int32>(Save.regions),
			Save.lastMilliseconds, static_cast<int32>(State.Build.Saver->dirtyRegions()));
		if (!Save.lastError.empty())
		{
			Text += TEXT("; last save failed: ") + TerrainWorldConversions::ToFString(Save.lastError);
		}
		return Text;
	}

	void RunEditStats(const TArray<FString>& Args, UWorld* World)
	{
		static_cast<void>(Args);
		if (World == nullptr)
		{
			return;
		}
		for (TActorIterator<ATerrainWorld> It(World); It; ++It)
		{
			FTerrainWorldState* State = It->bDevTools ? FTerrainBuildHooks::State(**It) : nullptr;
			if (State == nullptr)
			{
				continue;
			}
			const FString Text = EditStats(*State);
			UE_LOG(LogTerrainEngine, Log, TEXT("Terrain World: %s"), *Text);
			if (GEngine != nullptr)
			{
				GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::White, Text);
			}
		}
	}

	FAutoConsoleCommandWithWorldAndArgs EditStatsCommand(TEXT("Terrain.EditStats"),
		TEXT("The time from a click of the player to its tile shown again (the last edits), and "
			 "the saves of the edits"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunEditStats));
} // namespace

#endif // TERRAIN_DEV_TOOLS

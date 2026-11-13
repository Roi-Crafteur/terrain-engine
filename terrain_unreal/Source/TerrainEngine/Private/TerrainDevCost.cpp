// The console command Terrain.Cost of the tools of the developers (agent 40): the cost on the GPU
// of a switch of the look, measured in the same window for both sides, so that a change of the
// size of the window between two measures cannot fake it. Terrain.Cost [<seconds> [rounds]]
// <command A> | <command B> runs A and B in turn (A B B A A B), throws away the first 1.5 s after
// each switch and averages the GPU time of the frames over the rest of each phase, as the page of
// the performance of F3 reads it (Bridge/dev_ab_cost.hpp). The log gives each phase, the means of A
// and B, their spread, their difference and the size of the view at the start and the end.
// Never in a Shipping build.
#include "CoreMinimal.h"

#if TERRAIN_DEV_TOOLS

#include "Bridge/dev_ab_cost.hpp"
#include "Containers/Ticker.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "RenderTimer.h"
#include "TerrainDevState.h"
#include "TerrainEngineLog.h"
#include "UnrealClient.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace
{
	// The measure running: one at a time.
	struct FTerrainCostRun
	{
		TWeakObjectPtr<UWorld> World;
		terrain::unreal::CostCommand Command;
		std::optional<terrain::unreal::AbCost> Cost;
		std::size_t Logged = 0;
		FIntPoint StartSize = FIntPoint::ZeroValue;
		FTSTicker::FDelegateHandle Ticker;
	};

	FTerrainCostRun& TerrainCostRun()
	{
		static FTerrainCostRun Run;
		return Run;
	}

	FIntPoint TerrainCostViewSize(UWorld* World)
	{
		UGameViewportClient* Client = World != nullptr ? World->GetGameViewport() : nullptr;
		FViewport* Viewport = Client != nullptr ? Client->Viewport : nullptr;
		return Viewport != nullptr ? Viewport->GetSizeXY() : FIntPoint::ZeroValue;
	}

	void TerrainCostSide(UWorld* World, int Side)
	{
		const FTerrainCostRun& Run = TerrainCostRun();
		GEngine->Exec(World, *TerrainDev::ToText(Side == 0 ? Run.Command.a : Run.Command.b));
	}

	// The phases closed since the last call, then the summary once done.
	void LogTerrainCost(UWorld* World)
	{
		FTerrainCostRun& Run = TerrainCostRun();
		const std::vector<terrain::unreal::AbPhase>& Phases = Run.Cost->phases();
		const int32 Count = 2 * Run.Command.params.rounds;
		for (; Run.Logged < Phases.size(); ++Run.Logged)
		{
			const terrain::unreal::AbPhase& Phase = Phases[Run.Logged];
			UE_LOG(LogTerrainEngine, Log,
				TEXT("Terrain.Cost: phase %d of %d, %s: %.3f ms over %d frames"),
				static_cast<int32>(Run.Logged) + 1, Count, Phase.side == 0 ? TEXT("A") : TEXT("B"),
				Phase.meanGpuMs, Phase.frames);
		}
		if (!Run.Cost->done())
		{
			return;
		}
		const terrain::unreal::AbSummary Summary = Run.Cost->summary();
		const FIntPoint EndSize = TerrainCostViewSize(World);
		UE_LOG(LogTerrainEngine, Log,
			TEXT("Terrain.Cost: A %.3f ms (spread %.3f), B %.3f ms (spread %.3f), B - A = %+.3f ms "
				 "on the GPU; view %dx%d at the start, %dx%d at the end; B ran last"),
			Summary.mean[0], Summary.spread[0], Summary.mean[1], Summary.spread[1],
			Summary.difference, Run.StartSize.X, Run.StartSize.Y, EndSize.X, EndSize.Y);
		if (EndSize != Run.StartSize)
		{
			UE_LOG(LogTerrainEngine, Warning,
				TEXT("Terrain.Cost: the size of the view changed during the measure: measure "
					 "again"));
		}
	}

	bool TickTerrainCost(float)
	{
		FTerrainCostRun& Run = TerrainCostRun();
		UWorld* World = Run.World.Get();
		if (World == nullptr || !Run.Cost)
		{
			Run.Cost.reset();
			return false; // the play ended: the ticker goes
		}
		const double GpuMs = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
		if (const std::optional<int> Side = Run.Cost->addFrame(FPlatformTime::Seconds(), GpuMs))
		{
			TerrainCostSide(World, *Side);
		}
		LogTerrainCost(World);
		if (Run.Cost->done())
		{
			Run.Cost.reset();
			return false;
		}
		return true;
	}

	// Terrain.Cost [<seconds> [rounds]] <command A> | <command B>
	void RunTerrainCost(const TArray<FString>& Args, UWorld* World)
	{
		FTerrainCostRun& Run = TerrainCostRun();
		if (World == nullptr || !World->IsGameWorld())
		{
			UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain.Cost: only while playing"));
			return;
		}
		if (Run.Cost && Run.World.Get() != nullptr)
		{
			UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain.Cost: a measure is already running"));
			return;
		}
		std::vector<std::string> Utf8;
		for (const FString& Arg : Args)
		{
			Utf8.push_back(TerrainDev::ToUtf8(Arg));
		}
		std::string Error;
		const std::optional<terrain::unreal::CostCommand> Command =
			terrain::unreal::parseCost(Utf8, Error);
		if (!Command)
		{
			UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain.Cost: %s"), *TerrainDev::ToText(Error));
			return;
		}
		Run.World = World;
		Run.Command = *Command;
		Run.Cost.emplace(Command->params);
		Run.Logged = 0;
		Run.StartSize = TerrainCostViewSize(World);
		const terrain::unreal::AbCostParams& Params = Command->params;
		UE_LOG(LogTerrainEngine, Log,
			TEXT("Terrain.Cost: %d phases of %.1f + %.1f s (A B B A A B...), about %.0f s, view "
				 "%dx%d; A = %s, B = %s"),
			2 * Params.rounds, Params.warmupSeconds, Params.measureSeconds,
			2 * Params.rounds * (Params.warmupSeconds + Params.measureSeconds), Run.StartSize.X,
			Run.StartSize.Y, *TerrainDev::ToText(Command->a), *TerrainDev::ToText(Command->b));
		TerrainCostSide(World, Run.Cost->start(FPlatformTime::Seconds()));
		Run.Ticker =
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickTerrainCost));
	}

	FAutoConsoleCommandWithWorldAndArgs TerrainCostCommand(TEXT("Terrain.Cost"),
		TEXT("The cost on the GPU of a switch, A and B in turn in the same view: Terrain.Cost "
			 "[<seconds> [rounds]] <command A> | <command B>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunTerrainCost));
} // namespace

#endif // TERRAIN_DEV_TOOLS

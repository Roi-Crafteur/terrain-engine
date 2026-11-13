// The edits in the tools of the developers. Only with TERRAIN_DEV_TOOLS: never in a Shipping build.
// - Terrain.EditStats (agent 28): the time from a click of the player to the tile it changed
//   shown again, over the last edits (Bridge/edit_latency.hpp), and the saves of the edits
//   (Bridge/edit_save.hpp).
// - Terrain.EditAim <dig|place> <block|sphere> [radius] [at x y z] (agent 41): a click of the
//   player for the scripts, by its code (UTerrainBuildComponent::BuildRequested: the gate, the aim
//   from the camera, the reach, the capsule, the rate, the queue and so the save), toward the
//   center of the screen or through a point of the core; one line in the log each time
//   (Bridge/dev_commands.hpp: editAimLine).
// - Terrain.EditRead [0|1] (agent 41): the next play of this session of the editor reads and saves
//   the edits of its seed as with Building.bSaveEdits, without touching the actor or the map.
#include "CoreMinimal.h"

#if TERRAIN_DEV_TOOLS

#include "Bridge/dev_commands.hpp"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TerrainBuildAim.h"
#include "TerrainBuildComponent.h"
#include "TerrainEngineLog.h"
#include "TerrainWorld.h"
#include "TerrainWorldBuild.h"
#include "TerrainWorldState.h"

#include <string>
#include <vector>

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

	std::vector<std::string> Utf8Args(const TArray<FString>& Args)
	{
		std::vector<std::string> Out;
		for (const FString& Arg : Args)
		{
			Out.emplace_back(TCHAR_TO_UTF8(*Arg));
		}
		return Out;
	}

	// The request of a click from an EditAim: the tool, the radius of a sphere, the point of the
	// core the trace goes through, in Unreal.
	FTerrainBuildRequest RequestOf(const terrain::unreal::EditAimCommand& Command,
		const terrain::unreal::UnrealFrame& Frame)
	{
		FTerrainBuildRequest Request;
		Request.Action = Command.action == terrain::unreal::BuildAction::Break
			? ETerrainBuildAction::Break
			: ETerrainBuildAction::Place;
		Request.Tool = Command.tool == terrain::unreal::BuildTool::Block ? ETerrainBuildTool::Block
																		  : ETerrainBuildTool::Sphere2;
		if (Command.tool != terrain::unreal::BuildTool::Block)
		{
			Request.RadiusMeters = Command.radius;
		}
		if (Command.at)
		{
			const terrain::unreal::UnrealPoint Through = terrain::unreal::toUnreal(Frame, *Command.at);
			Request.Through = FVector(Through.x, Through.y, Through.z);
		}
		return Request;
	}

	// The edit of the pawn of the player through its Terrain Build, and its line in the log.
	void EditAim(ATerrainWorld& Terrain, FTerrainWorldState& State,
		const terrain::unreal::EditAimCommand& Command)
	{
		APlayerController* Controller = Terrain.GetWorld()->GetFirstPlayerController();
		APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
		UTerrainBuildComponent* Builder =
			Pawn != nullptr ? Pawn->FindComponentByClass<UTerrainBuildComponent>() : nullptr;
		terrain::unreal::EditAimOutcome Outcome;
		if (Builder == nullptr)
		{
			Outcome.refusal = "the pawn has no Terrain Build (Terrain.Pawn first)";
		}
		else
		{
			const bool bQueued = Builder->BuildRequested(RequestOf(Command, State.Frame));
			if (const FTerrainBuildAim* Aim = Builder->GetLastAim())
			{
				if (Aim->Hit)
				{
					Outcome.hit = Aim->Hit->point;
				}
				if (Aim->Target)
				{
					Outcome.edit = Aim->Target->edit;
				}
			}
			if (!bQueued)
			{
				Outcome.refusal = TCHAR_TO_UTF8(*Builder->GetLastRefusal());
			}
		}
		UE_LOG(LogTerrainEngine, Log, TEXT("Terrain dev: %s"),
			UTF8_TO_TCHAR(terrain::unreal::editAimLine(Command, Outcome).c_str()));
	}

	void RunEditAim(const TArray<FString>& Args, UWorld* World)
	{
		std::string Error;
		const std::vector<std::string> Utf8 = Utf8Args(Args);
		const std::optional<terrain::unreal::EditAimCommand> Command =
			terrain::unreal::parseEditAim(Utf8, Error);
		if (!Command)
		{
			UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain dev: %s"), UTF8_TO_TCHAR(Error.c_str()));
			return;
		}
		if (World == nullptr)
		{
			return;
		}
		for (TActorIterator<ATerrainWorld> It(World); It; ++It)
		{
			if (FTerrainWorldState* State = It->bDevTools ? FTerrainBuildHooks::State(**It) : nullptr)
			{
				EditAim(**It, *State, *Command);
			}
		}
	}

	FAutoConsoleCommandWithWorldAndArgs EditAimConsoleCommand(TEXT("Terrain.EditAim"),
		TEXT("A click of the player: Terrain.EditAim <dig|place> <block|sphere> [radius m] "
			 "[at x y z] (the point of the core the trace goes through; else the center of the "
			 "screen)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunEditAim));

	void RunEditRead(const TArray<FString>& Args)
	{
		std::string Error;
		const std::vector<std::string> Utf8 = Utf8Args(Args);
		const std::optional<terrain::unreal::EditReadCommand> Command =
			terrain::unreal::parseEditRead(Utf8, Error);
		if (!Command)
		{
			UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain dev: %s"), UTF8_TO_TCHAR(Error.c_str()));
			return;
		}
		if (Command->read)
		{
			FTerrainBuildHooks::SetReadOnce(*Command->read);
		}
		UE_LOG(LogTerrainEngine, Log, TEXT("Terrain dev: the next play reads the saved edits: %s"),
			FTerrainBuildHooks::ReadOnce() ? TEXT("yes") : TEXT("no"));
	}

	FAutoConsoleCommandWithArgs EditReadConsoleCommand(TEXT("Terrain.EditRead"),
		TEXT("1: the next play of this editor reads and saves the edits of its seed as with "
			 "Building.bSaveEdits, the actor and the map untouched; 0: not; nothing: the state"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&RunEditRead));
} // namespace

#endif // TERRAIN_DEV_TOOLS

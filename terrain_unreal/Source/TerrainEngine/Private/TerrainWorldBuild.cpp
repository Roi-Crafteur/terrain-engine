// Breaking and placing in game on the side of the world (agent 28): the edits of the players
// queued on the edit thread of the session, the build component given to the player pawn, the
// save of the edits of the seed (Bridge/edit_save.hpp: off by default, Building.bSaveEdits or the
// console command Terrain.EditSave 0|1; written on a thread of its own) and, only with
// TERRAIN_DEV_TOOLS, the time from a click to its tile shown (Terrain.EditStats,
// TerrainDevEdits.cpp).
#include "TerrainWorldBuild.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpectatorPawn.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "TerrainBuildComponent.h"
#include "TerrainDevMap.h"
#include "TerrainEngineLog.h"
#include "TerrainWorld.h"
#include "TerrainWorldState.h"

#include "terrain/storage/storage_error.hpp"
#include "terrain/storage/world_save.hpp"

#include <exception>
#include <filesystem>
#include <optional>
#include <vector>

namespace
{
	using TerrainWorldConversions::ToFString;

	// The folder of the edits of the seed of the world (Bridge/edit_save.hpp: seed_<seed>).
	FString SaveFolderText(const ATerrainWorld& World)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),
			World.Building.SaveFolder, FString::Printf(TEXT("seed_%llu"),
				static_cast<uint64>(World.Seed))));
	}

	std::filesystem::path SaveFolderOf(const ATerrainWorld& World)
	{
		return std::filesystem::path(*SaveFolderText(World));
	}

	// A message for the player and the log.
	void Tell(const FString& Message, bool bWarning)
	{
		if (bWarning)
		{
			UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain World: %s"), *Message);
		}
		else
		{
			UE_LOG(LogTerrainEngine, Log, TEXT("Terrain World: %s"), *Message);
		}
		if (GEngine != nullptr)
		{
			GEngine->AddOnScreenDebugMessage(-1, 8.0f, bWarning ? FColor::Yellow : FColor::White,
				Message);
		}
	}

	// Reads the saved edits of the seed into the session, and the metadata of their world into
	// Meta (kept by the saves of this play): false, nothing changed, when they are refused (the
	// reason told, the save then never written over this play).
	bool LoadSaved(FTerrainWorldState& State, const ATerrainWorld& World,
		const terrain::WorldSize& WorldSize, std::optional<terrain::WorldMeta>& Meta)
	{
		const uint64 Seed = static_cast<uint64>(World.Seed);
		terrain::unreal::EditLoad Load =
			terrain::unreal::loadEdits(SaveFolderOf(World), Seed, WorldSize);
		if (!Load.error.empty())
		{
			Tell(ToFString(Load.error) + TEXT(" (the save stays off for this play)"), true);
			return false;
		}
		for (const std::string& Warning : Load.warnings)
		{
			Tell(ToFString(Warning), true);
		}
		if (Load.found)
		{
			const std::size_t Blocks = Load.modifications.blockCount();
			const std::vector<terrain::ChunkCoord> Chunks =
				State.Session.replaceModifications(std::move(Load.modifications));
			const int64 Created = Load.meta.has_value() ? Load.meta->createdAt : 0;
			Tell(FString::Printf(TEXT("%d saved blocks of seed %llu read from %s (%d chunks; its "
						 "world created %s)"),
					 static_cast<int32>(Blocks), Seed, *SaveFolderText(World),
					 static_cast<int32>(Chunks.size()),
					 *FDateTime::FromUnixTimestamp(Created).ToIso8601()),
				false);
		}
		Meta = std::move(Load.meta);
		return true;
	}

	void StartSaver(ATerrainWorld& World, FTerrainWorldState& State, bool bLoad)
	{
		const std::filesystem::path Folder = SaveFolderOf(World);
		const terrain::WorldSize WorldSize = State.Session.tiles().world();
		const uint64 Seed = static_cast<uint64>(World.Seed);
		std::optional<terrain::WorldMeta> Loaded;
		if (bLoad && !LoadSaved(State, World, WorldSize, Loaded))
		{
			State.Build.bSaveRefused = true;
			return;
		}
		// The metadata of the world read, or of the world already in the folder: the saves keep
		// its name, creation, spawn and play time, never the defaults over them.
		terrain::WorldMeta Meta = Loaded.has_value()
									  ? std::move(*Loaded)
									  : terrain::unreal::keptWorldMeta(Folder, Seed, WorldSize);
		terrain::unreal::TerrainSession& Session = State.Session;
		State.Build.Saver = std::make_unique<terrain::unreal::EditSaver>(Folder, std::move(Meta),
			[&Session] { return Session.tiles().modifications(); });
		State.Build.LastSaveSeconds = FPlatformTime::Seconds();
	}

	// Terrain.EditSave 0|1 on every Terrain World of the world.
	void RunEditSave(const TArray<FString>& Args, UWorld* World)
	{
		if (World == nullptr || Args.Num() != 1 || (Args[0] != TEXT("0") && Args[0] != TEXT("1")))
		{
			UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain.EditSave 0|1"));
			return;
		}
		for (TActorIterator<ATerrainWorld> It(World); It; ++It)
		{
			FTerrainBuildHooks::SetSave(**It, Args[0] == TEXT("1"));
		}
	}

	FAutoConsoleCommandWithWorldAndArgs EditSaveCommand(TEXT("Terrain.EditSave"),
		TEXT("Saves the edits of the players of the seed (1: from now on, the save of this seed "
			 "replaced by the edits of this play; 0: a last save, then no more)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunEditSave));
} // namespace

FTerrainWorldState* FTerrainBuildHooks::State(ATerrainWorld& World)
{
	return World.State.IsValid() && !World.Starting.IsValid() ? World.State.Get() : nullptr;
}

bool FTerrainBuildHooks::IsLoadingView(const ATerrainWorld& World)
{
	if (World.Starting.IsValid() || !World.State.IsValid())
	{
		return true;
	}
	const FTerrainLoadingState& Loading = World.State->Loading;
	return Loading.bScreenBlack ||
		   (Loading.Phase.IsSet() && (Loading.Phase->fading() || Loading.Phase->holding()));
}

bool FTerrainBuildHooks::IsDevMapOpen(const ATerrainWorld& World)
{
	return World.DevMap != nullptr && World.DevMap->IsDevMapOpen();
}

void FTerrainBuildHooks::QueueBuild(FTerrainWorldState& State, const terrain::VolumeEdit& Edit,
	const FString& What)
{
	State.Session.queueEdit(Edit, std::string(TerrainBuildLabel) + TCHAR_TO_UTF8(*What));
#if TERRAIN_DEV_TOOLS
	State.Build.Clicks.push_back(
		State.Build.Latency.clicked(FPlatformTime::Seconds(), static_cast<int64>(GFrameCounter)));
#endif
}

void NoteBuildEdit(FTerrainWorldState& State, const terrain::unreal::EditOutcome& Outcome)
{
	if (Outcome.error == nullptr && State.Build.Saver != nullptr)
	{
		State.Build.Saver->markEdited(Outcome.chunks);
	}
#if TERRAIN_DEV_TOOLS
	if (Outcome.label.rfind(TerrainBuildLabel, 0) == 0 && !State.Build.Clicks.empty())
	{
		const std::uint64_t Id = State.Build.Clicks.front();
		State.Build.Clicks.pop_front();
		const std::vector<terrain::ChunkCoord> None;
		State.Build.Latency.done(Id, Outcome.error == nullptr ? Outcome.chunks : None,
			FPlatformTime::Seconds());
	}
#endif
}

void FTerrainBuildHooks::Start(ATerrainWorld& World)
{
	ApplyEditLight(World);
	if (World.State.IsValid() && World.Building.bSaveEdits)
	{
		StartSaver(World, *World.State.Get(), true);
	}
}

void FTerrainBuildHooks::Tick(ATerrainWorld& World, APawn* Pawn)
{
	FTerrainBuildState& Build = World.State->Build;
	const double Now = FPlatformTime::Seconds();
	if (Build.Saver != nullptr && Build.Saver->dirtyRegions() > 0 &&
		Now - Build.LastSaveSeconds >= World.Building.SaveIntervalSeconds)
	{
		const terrain::unreal::EditSaver::Report Last = Build.Saver->report();
		if (!Last.lastError.empty())
		{
			UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain World: the last save of the edits "
				"failed, written again now: %s"), *ToFString(Last.lastError));
		}
		Build.Saver->save();
		Build.LastSaveSeconds = Now;
	}
	if (World.Building.bAddToPlayerPawn && Pawn != nullptr && Pawn->IsPlayerControlled() &&
		!Pawn->IsA<ASpectatorPawn>() && Pawn->FindComponentByClass<UTerrainBuildComponent>() == nullptr)
	{
		UTerrainBuildComponent* Component = NewObject<UTerrainBuildComponent>(Pawn);
		Component->RegisterComponent();
		UE_LOG(LogTerrainEngine, Log, TEXT("Terrain World: breaking and placing given to %s"),
			*Pawn->GetName());
	}
}

void FTerrainBuildHooks::Stop(ATerrainWorld& World)
{
	if (World.State.IsValid() && World.State->Build.Saver != nullptr)
	{
		// The edits still on the edit thread (a click just before the end): done, then marked for
		// the last save, which would otherwise leave them out.
		FTerrainWorldState& Playing = *World.State.Get();
		try
		{
			for (const terrain::unreal::EditOutcome& Outcome : Playing.Session.takeAllEdits())
			{
				NoteBuildEdit(Playing, Outcome);
			}
		}
		catch (const std::exception& Exception)
		{
			UE_LOG(LogTerrainEngine, Error, TEXT("Terrain World: the last edits not staged: %s"),
				*ToFString(Exception.what()));
		}
		World.State->Build.Saver->save();
		World.State->Build.Saver->flush();
		const terrain::unreal::EditSaver::Report Last = World.State->Build.Saver->report();
		const FString Failure =
			Last.lastError.empty() ? FString() : TEXT(", the last failed: ") + ToFString(Last.lastError);
		UE_LOG(LogTerrainEngine, Log, TEXT("Terrain World: edits saved %d times this play, the last "
			"in %.1f ms%s"), static_cast<int32>(Last.saves), Last.lastMilliseconds, *Failure);
		World.State->Build.Saver.reset();
	}
}

bool FTerrainBuildHooks::SetSave(ATerrainWorld& World, bool bSave)
{
	FTerrainWorldState* Playing = State(World);
	if (Playing == nullptr)
	{
		Tell(TEXT("no save of the edits outside play"), true);
		return false;
	}
	if (!bSave)
	{
		Stop(World);
		Tell(TEXT("the edits are no longer saved"), false);
		return true;
	}
	if (Playing->Build.bSaveRefused || Playing->Build.Saver != nullptr)
	{
		Tell(Playing->Build.bSaveRefused
				 ? TEXT("the saved edits of this seed were refused at the start: move their folder "
						"away first")
				 : TEXT("the edits are already saved"),
			Playing->Build.bSaveRefused);
		return !Playing->Build.bSaveRefused;
	}
	StartSaver(World, *Playing, false);
	// The save of this folder replaced: its regions and the regions of every edit written again.
	std::vector<terrain::RegionCoord> Regions;
	try
	{
		Regions = terrain::savedRegions(SaveFolderOf(World));
	}
	catch (const std::exception&)
	{
		// No readable folder: nothing to replace.
	}
	Playing->Build.Saver->markRegions(Regions);
	std::vector<terrain::ChunkCoord> Chunks;
	for (const auto& Entry : Playing->Session.tiles().modifications().byChunk())
	{
		Chunks.push_back(Entry.first);
	}
	Playing->Build.Saver->markEdited(Chunks);
	Playing->Build.Saver->save();
	Tell(FString::Printf(TEXT("the edits of seed %lld are saved in %s from now on"), World.Seed,
			 *SaveFolderText(World)),
		false);
	return true;
}

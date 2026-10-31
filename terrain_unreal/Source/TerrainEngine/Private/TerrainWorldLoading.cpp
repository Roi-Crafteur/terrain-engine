// The measures of the loads of ATerrainWorld (agent 38): the timeline of each load at the spawn,
// after each teleport and around a prepared point, its milestones logged as they are reached, its
// summary, its curve and its CSV; the check of the build on a thread. The phase of the loads (the
// boost, the fade, the hold, the prepared point): TerrainWorldLoadingPhase.cpp.
#include "TerrainWorldLoading.h"

#include "TerrainWorld.h"

#include "Bridge/load_timeline.hpp"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderTimer.h"
#include "TerrainEngineLog.h"
#include "TerrainWorldState.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using TerrainWorldConversions::ToFString;
	using TerrainWorldConversions::ToUnrealPoint;
	using terrain::unreal::LoadClock;
	using terrain::unreal::LoadMilestone;
	using terrain::unreal::LoadTimeline;

	// A load not complete after this long is reported as it stands, its missing milestones named.
	constexpr double LoadReportAfterSeconds = 60.0;
	// The trace down to the ground under the pawn, from and to this far above and below it, cm.
	constexpr double LoadGroundTraceCm = 5000.0;

	FString LoadReasonText(const LoadTimeline& Timeline)
	{
		return ToFString(terrain::unreal::loadReasonName(Timeline.summary().reason));
	}

	// The collision of the tiles under the pawn: a trace down through the ground of the core there
	// hits a tile of this world.
	bool LoadGroundCollision(const ATerrainWorld& World, const FTerrainWorldState& State,
		const APawn& Pawn)
	{
		const FVector Location = Pawn.GetActorLocation();
		const double GroundZ = State.Session.groundZ(State.Frame, ToUnrealPoint(Location));
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TerrainGroundReady), false, &Pawn);
		FHitResult Hit;
		const FVector Start(Location.X, Location.Y, GroundZ + LoadGroundTraceCm);
		const FVector End(Location.X, Location.Y, GroundZ - LoadGroundTraceCm);
		const UWorld* Level = World.GetWorld();
		return Level != nullptr &&
			Level->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params) &&
			Hit.GetActor() == &World;
	}

	// The levels of the far horizon shown around the player, how many it has (0: none), and
	// whether every one is shown with its detail.
	void LoadHorizonLevels(FTerrainWorldState& State, int& OutShown, int& OutCount, bool& OutIdle)
	{
		OutShown = 0;
		OutCount = 0;
		OutIdle = false;
		if (State.HorizonGpu != nullptr)
		{
			OutCount = State.HorizonGpu->params().levels;
			OutShown = State.HorizonGpu->levelsShown();
			OutIdle = OutShown == OutCount && !State.HorizonGpu->busy();
		}
		else if (State.Horizon != nullptr)
		{
			OutCount = State.Horizon->params().levels;
			OutShown = State.Horizon->shown() ? OutCount : 0;
		}
	}

	void MarkLoadHorizon(LoadTimeline& Timeline, int Shown, int Count, bool bIdle,
		LoadClock::time_point Now, std::vector<LoadMilestone>& Reached)
	{
		const int Near = std::min(terrain::unreal::kNearHorizonLevels, Count);
		const std::pair<LoadMilestone, bool> Marks[] = {{LoadMilestone::HorizonNear, Shown >= Near},
			{LoadMilestone::HorizonAll, Shown == Count}, {LoadMilestone::HorizonDetail, bIdle}};
		for (const auto& [Milestone, bDone] : Marks)
		{
			if (Count > 0 && bDone && Timeline.mark(Milestone, Now))
			{
				Reached.push_back(Milestone);
			}
		}
	}

	void WriteLoadCsv(const LoadTimeline& Timeline, int32 Index)
	{
		TArray<FString> Rows;
		for (const std::string& Row : terrain::unreal::csvRows(Timeline.traces()))
		{
			Rows.Add(ToFString(Row));
		}
		const FString Folder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TerrainEngine"),
			TEXT("Loading"));
		IFileManager::Get().MakeDirectory(*Folder, true);
		const FString Reason = LoadReasonText(Timeline).Replace(TEXT(" "), TEXT("_"));
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(Folder,
			FString::Printf(TEXT("load_%s_%d_%s.csv"),
				*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")), Index, *Reason)));
		if (FFileHelper::SaveStringArrayToFile(Rows, *Path))
		{
			UE_LOG(LogTerrainEngine, Log, TEXT("  tiles of the load (CSV): %s"), *Path);
		}
		else
		{
			UE_LOG(LogTerrainEngine, Warning, TEXT("  cannot write the CSV of the load: %s"), *Path);
		}
	}

	// The summary, the curve and the CSV of the current load, as it stands.
	void ReportLoad(FTerrainWorldState& State)
	{
		State.Loading.bReported = true;
		const LoadTimeline& Timeline = State.Session.timeline();
		UE_LOG(LogTerrainEngine, Log, TEXT("Terrain World: load %d report"), State.Loading.Loads);
		for (const std::string& Line : terrain::unreal::describe(Timeline.summary()))
		{
			UE_LOG(LogTerrainEngine, Log, TEXT("  %s"), *ToFString(Line));
		}
		FString Missing;
		for (const LoadMilestone Milestone : Timeline.missing())
		{
			Missing += (Missing.IsEmpty() ? TEXT("") : TEXT(", ")) +
				ToFString(terrain::unreal::milestoneName(Milestone));
		}
		if (!Missing.IsEmpty())
		{
			UE_LOG(LogTerrainEngine, Warning, TEXT("  not reached when reported: %s"), *Missing);
		}
		WriteLoadCsv(Timeline, State.Loading.Loads);
	}

	void LogLoadMilestones(FTerrainWorldState& State, const std::vector<LoadMilestone>& Reached)
	{
		if (Reached.empty())
		{
			return;
		}
		const LoadTimeline& Timeline = State.Session.timeline();
		const terrain::unreal::LoadSummary Summary = Timeline.summary();
		for (const LoadMilestone Milestone : Reached)
		{
			UE_LOG(LogTerrainEngine, Log, TEXT("Terrain World: load %d (%s): %s"),
				State.Loading.Loads, *LoadReasonText(Timeline),
				*ToFString(terrain::unreal::describeMilestone(Milestone,
					Summary.milestones[static_cast<std::size_t>(Milestone)])));
		}
	}
} // namespace

void StartDeterminismCheck(FTerrainWorldState& State)
{
	State.Loading.DeterminismStartSeconds = FPlatformTime::Seconds();
	// A world of its own, nothing shared with the session: it runs while the game starts.
	State.Loading.Determinism =
		std::async(std::launch::async, [] { return terrain::unreal::checkDefaultWorld(); });
}

bool PollDeterminismCheck(FTerrainWorldState& State, FString& OutError)
{
	std::future<terrain::unreal::DeterminismReport>& Check = State.Loading.Determinism;
	if (!Check.valid() || Check.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
	{
		return true;
	}
	terrain::unreal::DeterminismReport Result;
	try
	{
		Result = Check.get();
	}
	catch (const std::exception& Exception)
	{
		OutError = FString::Printf(TEXT("the check of the world failed: %s"),
			*ToFString(Exception.what()));
		return false;
	}
	const FString Text = ToFString(terrain::unreal::describe(Result));
	if (!Result.same)
	{
		OutError = FString::Printf(TEXT("%s. No streaming."), *Text);
		return false;
	}
	UE_LOG(LogTerrainEngine, Log, TEXT("Terrain World: %s (checked on a thread in %.2f s)"), *Text,
		FPlatformTime::Seconds() - State.Loading.DeterminismStartSeconds);
	return true;
}

void BeginTerrainLoad(FTerrainWorldState& State, const FVector& Focus,
	terrain::unreal::LoadReason Reason)
{
	if (!State.Loading.bReported)
	{
		ReportLoad(State); // the previous load, as it stands
	}
	const terrain::unreal::CorePoint Core =
		terrain::unreal::toCore(State.Frame, ToUnrealPoint(Focus));
	terrain::unreal::LoadExpectations Expectations;
	// Without a pawn the collision is marked at once; a prepared point has no pawn on it.
	Expectations.groundCollision = Reason != terrain::unreal::LoadReason::Prepared;
	Expectations.horizon = State.HorizonGpu != nullptr || State.Horizon != nullptr;
	Expectations.horizonDetail =
		State.HorizonGpu != nullptr && State.HorizonGpu->detail() != nullptr;
	LoadTimeline& Timeline = State.Session.timeline();
	Timeline.begin(Reason, Core.x, Core.z, LoadClock::now(), Expectations);
	if (State.Loading.Phase.IsSet())
	{
		State.Loading.Phase->begin(FPlatformTime::Seconds());
		State.Loading.bBoosted = true;
	}
	++State.Loading.Loads;
	State.Loading.bReported = false;
	if (Reason != terrain::unreal::LoadReason::Prepared)
	{
		// The chunk of the pawn, to detect the next teleport.
		State.Loading.LastChunk = terrain::wrapChunk(terrain::worldToChunk(Core.x, Core.z),
			State.Session.tiles().world());
		State.Loading.bHasLastChunk = true;
	}
	UE_LOG(LogTerrainEngine, Log,
		TEXT("Terrain World: load %d (%s) started at (%.1f, %.1f) m, %d tiles shown, %d pending"),
		State.Loading.Loads, *LoadReasonText(Timeline), Core.x, Core.z,
		static_cast<int32>(State.Session.streamer().shownCount()),
		static_cast<int32>(State.Session.streamer().manager().pendingCount()));
}

void TickTerrainLoading(ATerrainWorld& World, FTerrainWorldState& State, const FVector& Focus,
	APawn* Pawn, const FTerrainUploads& Uploads)
{
	LoadTimeline& Timeline = State.Session.timeline();
	const LoadClock::time_point Now = LoadClock::now();
	const terrain::unreal::UnrealPoint Point = ToUnrealPoint(Focus);
	Timeline.recordFrame(FPlatformTime::ToMilliseconds(GGameThreadTime),
		static_cast<std::size_t>(FMath::Max(0, Uploads.Count)));
	const std::vector<terrain::unreal::SelectedTile> Selection = terrain::unreal::selectedTiles(
		State.Session.streamer(), terrain::unreal::chunkAt(State.Frame, Point));
	std::vector<LoadMilestone> Reached = Timeline.observe(Selection, Now);
	// The collision under the pawn, traced while the load or the hold waits for it.
	const bool bWaits = !Timeline.reached(LoadMilestone::GroundCollision) ||
		(State.Loading.Phase.IsSet() && State.Loading.Phase->holding());
	const bool bCollision =
		!bWaits || Pawn == nullptr || LoadGroundCollision(World, State, *Pawn);
	if (bCollision && Timeline.mark(LoadMilestone::GroundCollision, Now))
	{
		Reached.push_back(LoadMilestone::GroundCollision);
	}
	int Shown = 0;
	int Count = 0;
	bool bIdle = false;
	LoadHorizonLevels(State, Shown, Count, bIdle);
	MarkLoadHorizon(Timeline, Shown, Count, bIdle, Now, Reached);
	if (!State.Loading.bReported)
	{
		LogLoadMilestones(State, Reached);
	}
	const terrain::unreal::TileRings Rings = terrain::unreal::tileRings(Selection);
	const bool bAll = Rings.all && State.Session.streamer().manager().pendingCount() == 0;
	UpdateTerrainPhase(World, State, Pawn,
		{Rings.ground, bCollision, Rings.ring64, bAll, Shown, Count}, Uploads);
	if (!State.Loading.bReported &&
		(Timeline.complete() || Timeline.secondsSince(Now) > LoadReportAfterSeconds))
	{
		ReportLoad(State);
	}
}

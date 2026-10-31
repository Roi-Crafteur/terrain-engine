// The measures of the loads of ATerrainWorld (agent 38): the timeline of each load at the spawn and
// after each teleport, its milestones logged as they are reached, its summary, its curve and its
// CSV; the console command Terrain.LoadTest. Measures only: nothing here changes the streaming.
#include "TerrainWorldLoading.h"

#include "TerrainWorld.h"

#include "Bridge/load_timeline.hpp"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderTimer.h"
#include "TerrainEngineLog.h"
#include "TerrainWorldState.h"

#include <string>
#include <vector>

namespace
{
	using TerrainWorldConversions::ToFString;
	using TerrainWorldConversions::ToUnrealPoint;
	using terrain::unreal::LoadClock;
	using terrain::unreal::LoadMilestone;
	using terrain::unreal::LoadTimeline;

	// A load not complete after this long is reported as it stands, its missing milestones named.
	constexpr double ReportAfterSeconds = 60.0;
	// The trace down to the ground under the pawn, from and to this far above and below it, cm.
	constexpr double GroundTraceCm = 5000.0;
	// Above the ground, the bottom of a pawn placed there by Terrain.LoadTest, centimeters.
	constexpr double LoadTestClearanceCm = 50.0;

	terrain::ChunkCoord CanonicalChunk(const FTerrainWorldState& State, const FVector& Focus)
	{
		const terrain::unreal::CorePoint Core =
			terrain::unreal::toCore(State.Frame, ToUnrealPoint(Focus));
		return terrain::wrapChunk(
			terrain::worldToChunk(Core.x, Core.z), State.Session.tiles().world());
	}

	FString ReasonName(const LoadTimeline& Timeline)
	{
		return ToFString(terrain::unreal::loadReasonName(Timeline.summary().reason));
	}

	// The collision of the tiles under the pawn: a trace down through the ground of the core there
	// hits a tile of this world.
	bool GroundCollisionUnder(const ATerrainWorld& World, const FTerrainWorldState& State,
		const APawn& Pawn)
	{
		const FVector Location = Pawn.GetActorLocation();
		const double GroundZ = State.Session.groundZ(State.Frame, ToUnrealPoint(Location));
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TerrainGroundReady), false, &Pawn);
		FHitResult Hit;
		const FVector Start(Location.X, Location.Y, GroundZ + GroundTraceCm);
		const FVector End(Location.X, Location.Y, GroundZ - GroundTraceCm);
		const UWorld* Level = World.GetWorld();
		return Level != nullptr &&
			Level->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params) &&
			Hit.GetActor() == &World;
	}

	// The far horizon placed around the focus, then its detail all in.
	void MarkHorizon(FTerrainWorldState& State, const terrain::unreal::CorePoint& Core,
		LoadClock::time_point Now, std::vector<LoadMilestone>& Reached)
	{
		LoadTimeline& Timeline = State.Session.timeline();
		bool bAround = false;
		bool bIdle = false;
		if (State.HorizonGpu != nullptr)
		{
			bAround = terrain::unreal::horizonAround(State.HorizonGpu->clipmap(), Core.x, Core.z);
			bIdle = bAround && !State.HorizonGpu->busy();
		}
		else if (State.Horizon != nullptr)
		{
			bAround = State.Horizon->shown();
		}
		if (bAround && Timeline.mark(LoadMilestone::Horizon, Now))
		{
			Reached.push_back(LoadMilestone::Horizon);
		}
		if (bIdle && Timeline.mark(LoadMilestone::HorizonDetail, Now))
		{
			Reached.push_back(LoadMilestone::HorizonDetail);
		}
	}

	void WriteCsv(const LoadTimeline& Timeline, int32 Index)
	{
		TArray<FString> Rows;
		for (const std::string& Row : terrain::unreal::csvRows(Timeline.traces()))
		{
			Rows.Add(ToFString(Row));
		}
		const FString Folder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TerrainEngine"),
			TEXT("Loading"));
		IFileManager::Get().MakeDirectory(*Folder, true);
		const FString Reason = ReasonName(Timeline).Replace(TEXT(" "), TEXT("_"));
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
	void Report(FTerrainWorldState& State)
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
		WriteCsv(Timeline, State.Loading.Loads);
	}
} // namespace

void BeginTerrainLoad(FTerrainWorldState& State, const FVector& Focus, bool bTeleport)
{
	if (!State.Loading.bReported)
	{
		Report(State); // the previous load, as it stands
	}
	const terrain::unreal::CorePoint Core =
		terrain::unreal::toCore(State.Frame, ToUnrealPoint(Focus));
	terrain::unreal::LoadExpectations Expectations;
	Expectations.groundCollision = true; // marked at once without a pawn
	Expectations.horizon = State.HorizonGpu != nullptr || State.Horizon != nullptr;
	Expectations.horizonDetail =
		State.HorizonGpu != nullptr && State.HorizonGpu->detail() != nullptr;
	LoadTimeline& Timeline = State.Session.timeline();
	Timeline.begin(bTeleport ? terrain::unreal::LoadReason::Teleport
							 : terrain::unreal::LoadReason::Spawn,
		Core.x, Core.z, LoadClock::now(), Expectations);
	++State.Loading.Loads;
	State.Loading.bReported = false;
	State.Loading.LastChunk = CanonicalChunk(State, Focus);
	State.Loading.bHasLastChunk = true;
	UE_LOG(LogTerrainEngine, Log,
		TEXT("Terrain World: load %d (%s) started at (%.1f, %.1f) m, %d tiles shown, %d pending"),
		State.Loading.Loads, *ReasonName(Timeline), Core.x, Core.z,
		static_cast<int32>(State.Session.streamer().shownCount()),
		static_cast<int32>(State.Session.streamer().manager().pendingCount()));
}

void NoteTerrainFocus(ATerrainWorld& World, FTerrainWorldState& State, const FVector& Focus)
{
	FTerrainLoadingState& Loading = State.Loading;
	if (!Loading.bHasLastChunk)
	{
		BeginTerrainLoad(State, Focus, false);
		return;
	}
	const terrain::ChunkCoord Chunk = CanonicalChunk(State, Focus);
	const int Jump = terrain::unreal::wrappedChunkDistance(
		Loading.LastChunk, Chunk, State.Session.tiles().world());
	Loading.LastChunk = Chunk;
	if (Jump > World.ViewDistanceChunks)
	{
		UE_LOG(LogTerrainEngine, Log, TEXT("Terrain World: the focus jumped %d chunks: a teleport"),
			Jump);
		BeginTerrainLoad(State, Focus, true);
	}
}

void TickTerrainLoading(ATerrainWorld& World, FTerrainWorldState& State, const FVector& Focus,
	APawn* Pawn, int32 Uploads)
{
	if (State.Loading.bReported)
	{
		return;
	}
	LoadTimeline& Timeline = State.Session.timeline();
	const LoadClock::time_point Now = LoadClock::now();
	const terrain::unreal::UnrealPoint Point = ToUnrealPoint(Focus);
	const terrain::unreal::CorePoint Core = terrain::unreal::toCore(State.Frame, Point);
	Timeline.recordFrame(FPlatformTime::ToMilliseconds(GGameThreadTime),
		static_cast<std::size_t>(FMath::Max(0, Uploads)));
	std::vector<LoadMilestone> Reached = Timeline.observe(
		terrain::unreal::selectedTiles(State.Session.streamer(),
			terrain::unreal::chunkAt(State.Frame, Point)),
		Now);
	const bool bCollision = !Timeline.reached(LoadMilestone::GroundCollision) &&
		(Pawn == nullptr || GroundCollisionUnder(World, State, *Pawn));
	if (bCollision && Timeline.mark(LoadMilestone::GroundCollision, Now))
	{
		Reached.push_back(LoadMilestone::GroundCollision);
	}
	MarkHorizon(State, Core, Now, Reached);
	if (!Reached.empty())
	{
		const terrain::unreal::LoadSummary Summary = Timeline.summary();
		for (const LoadMilestone Milestone : Reached)
		{
			UE_LOG(LogTerrainEngine, Log, TEXT("Terrain World: load %d (%s): %s"),
				State.Loading.Loads, *ReasonName(Timeline),
				*ToFString(terrain::unreal::describeMilestone(Milestone,
					Summary.milestones[static_cast<std::size_t>(Milestone)])));
		}
	}
	if (Timeline.complete() || Timeline.secondsSince(Now) > ReportAfterSeconds)
	{
		Report(State);
	}
}

bool ATerrainWorld::LoadTest(double CoreX, double CoreZ, float HeadingDegrees, float PitchDegrees)
{
	FVector Focus = FVector::ZeroVector;
	APawn* Pawn = nullptr;
	if (!State.IsValid() || !FindFocus(Focus, Pawn) || Pawn == nullptr)
	{
		UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain.LoadTest: no streaming or no pawn"));
		return false;
	}
	const double Ground = State->Session.tiles().heightfield().heightAt(CoreX, CoreZ);
	const terrain::unreal::UnrealPoint OnGround =
		terrain::unreal::toUnreal(State->Frame, {CoreX, Ground, CoreZ});
	const FVector Location(OnGround.x, OnGround.y,
		OnGround.z + Pawn->GetSimpleCollisionHalfHeight() + LoadTestClearanceCm);
	Pawn->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	if (UPawnMovementComponent* Movement = Pawn->GetMovementComponent())
	{
		Movement->StopMovementImmediately();
	}
	if (AController* Controller = Pawn->GetController())
	{
		// The compass heading (0 north, 90 east) as the yaw of Unreal (0 along X, east).
		Controller->SetControlRotation(FRotator(PitchDegrees, HeadingDegrees - 90.0f, 0.0f));
	}
	UE_LOG(LogTerrainEngine, Log,
		TEXT("Terrain.LoadTest: %s teleported to (%.1f, %.1f) m, ground at %.2f m, heading %.1f, "
			 "pitch %.1f"),
		*Pawn->GetName(), CoreX, CoreZ, Ground, HeadingDegrees, PitchDegrees);
	BeginTerrainLoad(*State.Get(), Location, true);
	return true;
}

namespace
{
	// Terrain.LoadTest <x> <z> [heading] [pitch]: on every Terrain World of the world.
	void RunTerrainLoadTest(const TArray<FString>& Args, UWorld* World)
	{
		if (World == nullptr || Args.Num() < 2)
		{
			UE_LOG(LogTerrainEngine, Warning,
				TEXT("Terrain.LoadTest <x m> <z m> [heading degrees] [pitch degrees]"));
			return;
		}
		const float Heading = Args.Num() > 2 ? FCString::Atof(*Args[2]) : 0.0f;
		const float Pitch = Args.Num() > 3 ? FCString::Atof(*Args[3]) : 0.0f;
		for (TActorIterator<ATerrainWorld> It(World); It; ++It)
		{
			It->LoadTest(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]), Heading, Pitch);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs TerrainLoadTestCommand(TEXT("Terrain.LoadTest"),
		TEXT("Teleports the player to the core point (x, z), meters, on the ground, facing the "
			 "heading (0 north, 90 east) and the pitch, and measures the load there (log and CSV "
			 "in Saved/TerrainEngine/Loading): Terrain.LoadTest x z [heading] [pitch]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunTerrainLoadTest));
} // namespace

// The phase of the loads of ATerrainWorld (agent 38, Bridge/load_phase.hpp): more tile workers,
// horizon pieces and uploads within a cap while loading, the screen held black until the world
// around the player is shown, the pawn held until the collision under it, the prepared point;
// TeleportWhenReady, PrepareLocation and Terrain.LoadTest. The measures: TerrainWorldLoading.cpp.
#include "TerrainWorldLoading.h"

#include "TerrainWorld.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "TerrainEngineLog.h"
#include "TerrainWorldStart.h"
#include "TerrainWorldState.h"

#include <optional>
#include <string>
#include <vector>

namespace
{
	using TerrainWorldConversions::ToFString;
	using TerrainWorldConversions::ToUnrealPoint;

	// Above the ground, the bottom of a pawn placed there, centimeters.
	constexpr double LoadClearanceCm = 50.0;
	// The screen comes back from black over this time once the world around is shown.
	constexpr float LoadFadeInSeconds = 0.25f;
	// The level read for a teleport serves the pawn held within this distance of its point.
	constexpr double LoadKnownGroundMeters = 1.0;

	terrain::unreal::LoadBudget LoadNormalBudget(const ATerrainWorld& World)
	{
		return {FMath::Max(1, World.WorkerThreads), FMath::Max(1, World.HorizonThreads),
			static_cast<std::size_t>(FMath::Max(1, World.MaxTileUploadsPerFrame))};
	}

	terrain::ChunkCoord LoadCanonicalChunk(const FTerrainWorldState& State, const FVector& Focus)
	{
		const terrain::unreal::CorePoint Core =
			terrain::unreal::toCore(State.Frame, ToUnrealPoint(Focus));
		return terrain::wrapChunk(
			terrain::worldToChunk(Core.x, Core.z), State.Session.tiles().world());
	}

	// The Unreal position of the core point (x, z) at the height of the origin of the frame: no
	// ground read (up to 200 ms the first time in a region), the streaming and the horizon follow
	// x and z only.
	FVector LoadFocusPoint(const FTerrainWorldState& State, double X, double Z)
	{
		const terrain::unreal::UnrealPoint Point =
			terrain::unreal::toUnreal(State.Frame, {X, State.Frame.origin.y, Z});
		return FVector(Point.x, Point.y, Point.z);
	}

	APlayerCameraManager* LoadCamera(const ATerrainWorld& World)
	{
		APlayerController* Controller = UGameplayStatics::GetPlayerController(&World, 0);
		return Controller != nullptr ? Controller->PlayerCameraManager.Get() : nullptr;
	}

	// The screen black, or coming back from black: then without the black widget of the start,
	// the camera fade black still for this frame.
	void SetLoadBlack(const ATerrainWorld& World, FTerrainWorldState& State, bool bBlack)
	{
		if (!bBlack)
		{
			RemoveTerrainWidget(World, State.Loading.Cover);
		}
		APlayerCameraManager* Camera = LoadCamera(World);
		if (State.Loading.bScreenBlack == bBlack || Camera == nullptr)
		{
			return;
		}
		State.Loading.bScreenBlack = bBlack;
		if (bBlack)
		{
			Camera->SetManualCameraFade(1.0f, FLinearColor::Black, false);
		}
		else
		{
			Camera->StartCameraFade(1.0f, 0.0f, LoadFadeInSeconds, FLinearColor::Black, false, false);
		}
	}

	// The pawn held still (its movement off: no gravity) or given back, placed on the ground, or on
	// the water over it, if it stands below.
	void HoldLoadPawn(FTerrainWorldState& State, APawn* Pawn, bool bHold)
	{
		APawn* Held = State.Loading.HeldPawn.Get();
		if (bHold && Pawn != nullptr && Held != Pawn)
		{
			if (UPawnMovementComponent* Movement = Pawn->GetMovementComponent())
			{
				Movement->StopMovementImmediately();
				Movement->Deactivate();
			}
			State.Loading.HeldPawn = Pawn;
			return;
		}
		if (bHold || Held == nullptr)
		{
			return;
		}
		const FVector Location = Held->GetActorLocation();
		const double StandZ = TerrainLevelZ(State, Location, true);
		const double HalfHeight = Held->GetSimpleCollisionHalfHeight();
		if (Location.Z - HalfHeight < StandZ)
		{
			const FVector OnGround(Location.X, Location.Y, StandZ + HalfHeight + LoadClearanceCm);
			Held->SetActorLocation(OnGround, false, nullptr, ETeleportType::TeleportPhysics);
		}
		if (UPawnMovementComponent* Movement = Held->GetMovementComponent())
		{
			Movement->Activate();
		}
		State.Loading.HeldPawn.Reset();
	}

	void ApplyLoadBudget(FTerrainWorldState& State)
	{
		FTerrainLoadingState& Loading = State.Loading;
		if (!Loading.Phase.IsSet())
		{
			return;
		}
		const bool bPending = State.Session.streamer().manager().pendingCount() > 0;
		const terrain::unreal::LoadBudget Budget = Loading.Phase->budget(bPending);
		if (Budget == Loading.Applied)
		{
			return;
		}
		terrain::unreal::TileStreamer& Streamer = State.Session.streamer();
		Streamer.setWorkerLimit(Budget.workers);
		Streamer.setMaxLoadsPerUpdate(Budget.uploadsPerFrame);
		// The uploads of a frame stop before their estimated time goes beyond the budget, one at
		// least: the costs read on the game thread, where the uploads are measured.
		const terrain::unreal::UploadCostModel* Costs = &Loading.UploadCosts;
		Streamer.setLoadBudget([Costs](const terrain::TileData& Data)
			{
				return Costs->estimate(
					Data.mesh.vertices.size(), terrain::unreal::hasCollision(Data.request.key));
			},
			Budget.uploadMilliseconds);
		if (State.HorizonGpu != nullptr)
		{
			State.HorizonGpu->setMaxPiecesInFlight(Budget.pieces);
		}
		Loading.Applied = Budget;
	}

	void LogLoadFadeCap(FTerrainWorldState& State, const terrain::unreal::LoadStatus& Status)
	{
		const std::vector<std::string> Missing = State.Loading.Phase->takeMissingAtCap();
		if (Missing.empty())
		{
			return;
		}
		FString Names;
		for (const std::string& Name : Missing)
		{
			Names += (Names.IsEmpty() ? TEXT("") : TEXT(", ")) + ToFString(Name);
		}
		const terrain::unreal::LoadSummary Summary = State.Session.timeline().summary();
		const auto At = [&Summary](terrain::unreal::LoadMilestone Milestone)
		{
			return Summary.milestones[static_cast<std::size_t>(Milestone)];
		};
		UE_LOG(LogTerrainEngine, Warning,
			TEXT("Terrain World: load %d: the fade ended at its cap of %.1f s with %s still missing "
				 "(ground %.2f s, 64 m %.2f s, all tiles %.2f s, horizon %d of %d levels; -1: not "
				 "yet)"),
			State.Loading.Loads, State.Loading.Phase->params().fadeMaxSeconds, *Names,
			At(terrain::unreal::LoadMilestone::GroundCollision),
			At(terrain::unreal::LoadMilestone::Ring64), At(terrain::unreal::LoadMilestone::AllTiles),
			Status.horizonLevels, Status.horizonLevelCount);
	}
} // namespace

int32 TerrainLoadingCap(const ATerrainWorld& World)
{
	const terrain::unreal::LoadBudget Normal = LoadNormalBudget(World);
	if (World.LoadingSettings.ThreadCap > 0)
	{
		return FMath::Max(World.LoadingSettings.ThreadCap, Normal.workers + Normal.pieces);
	}
	return terrain::unreal::defaultLoadingCap(
		static_cast<unsigned>(FPlatformMisc::NumberOfCoresIncludingHyperthreads()), Normal);
}

void StartTerrainLoading(ATerrainWorld& World, FTerrainWorldState& State)
{
	const terrain::unreal::LoadBudget Normal = LoadNormalBudget(World);
	terrain::unreal::LoadPhaseParams Params;
	Params.normal = Normal;
	const FTerrainLoadingSettings& Settings = World.LoadingSettings;
	Params.cap = Settings.bBoost ? TerrainLoadingCap(World) : Normal.workers + Normal.pieces;
	Params.maxUploadsPerFrame = World.LoadingSettings.bBoost
		? FMath::Max<std::size_t>(Normal.uploadsPerFrame,
			static_cast<std::size_t>(FMath::Max(1, World.LoadingSettings.MaxUploadsPerFrame)))
		: Normal.uploadsPerFrame;
	Params.uploadMillisecondsPerFrame = FMath::Max(0.0f, Settings.UploadMillisecondsPerFrame);
	Params.fadeMaxSeconds = FMath::Max(0.0f, World.LoadingSettings.FadeMaxSeconds);
	Params.timeoutSeconds = FMath::Max(0.0f, World.LoadingSettings.TimeoutSeconds);
	Params.nearHorizonLevels = terrain::unreal::kNearHorizonLevels;
	Params.fade = World.LoadingSettings.bFade;
	State.Loading.Phase.Emplace(Params);
	State.Loading.Applied = Normal;
	State.Session.streamer().setWorkerLimit(Normal.workers);
	StartDeterminismCheck(State);
	UE_LOG(LogTerrainEngine, Log,
		TEXT("Terrain World: loads with %d tile workers and horizon pieces together at most (%d "
			 "hardware threads), up to %d uploads per frame within %.1f ms, the screen black %.1f s "
			 "at most%s"),
		Params.cap, FPlatformMisc::NumberOfCoresIncludingHyperthreads(),
		static_cast<int32>(Params.maxUploadsPerFrame), Params.uploadMillisecondsPerFrame,
		Params.fadeMaxSeconds, Params.fade ? TEXT("") : TEXT(" (no fade)"));
}

FVector BeginTerrainFrame(ATerrainWorld& World, FTerrainWorldState& State, const FVector& Focus)
{
	FTerrainLoadingState& Loading = State.Loading;
	const bool bPrepared = Loading.Phase.IsSet() && Loading.Phase->prepared().has_value();
	if (!Loading.bHasLastChunk)
	{
		BeginTerrainLoad(State, Focus, terrain::unreal::LoadReason::Spawn);
	}
	else if (!bPrepared)
	{
		const terrain::ChunkCoord Chunk = LoadCanonicalChunk(State, Focus);
		const int Jump = terrain::unreal::wrappedChunkDistance(
			Loading.LastChunk, Chunk, State.Session.tiles().world());
		Loading.LastChunk = Chunk;
		if (Jump > World.ViewDistanceChunks)
		{
			UE_LOG(LogTerrainEngine, Log,
				TEXT("Terrain World: the focus jumped %d chunks: a teleport"), Jump);
			BeginTerrainLoad(State, Focus, terrain::unreal::LoadReason::Teleport);
		}
	}
	ApplyLoadBudget(State);
	if (bPrepared)
	{
		const auto [X, Z] = *Loading.Phase->prepared();
		return LoadFocusPoint(State, X, Z);
	}
	return Focus;
}

void UpdateTerrainPhase(ATerrainWorld& World, FTerrainWorldState& State, APawn* Pawn,
	const terrain::unreal::LoadStatus& Status, const FTerrainUploads& Uploads)
{
	FTerrainLoadingState& Loading = State.Loading;
	if (!Loading.Phase.IsSet())
	{
		return;
	}
	terrain::unreal::LoadPhase& Phase = *Loading.Phase;
	const double Now = FPlatformTime::Seconds();
	Phase.update(Status, Now);
	SetLoadBlack(World, State, Phase.fading());
	HoldLoadPawn(State, Pawn, Phase.holding());
	LogLoadFadeCap(State, Status);
	if (Phase.takeReady())
	{
		const auto [X, Z] = *Phase.prepared();
		UE_LOG(LogTerrainEngine, Log,
			TEXT("Terrain World: the point (%.1f, %.1f) m is ready in %.2f s"), X, Z,
			Phase.elapsed(Now));
		World.OnTerrainLocationReady.Broadcast(X, Z, static_cast<float>(Phase.elapsed(Now)));
	}
	if (Loading.bBoosted && !Phase.loading())
	{
		Loading.bBoosted = false;
		UE_LOG(LogTerrainEngine, Log,
			TEXT("Terrain World: load %d: back to the normal regime after %.2f s"), Loading.Loads,
			Phase.elapsed(Now));
	}
}

void EndTerrainLoading(ATerrainWorld& World, FTerrainWorldState& State)
{
	SetLoadBlack(World, State, false);
	HoldLoadPawn(State, nullptr, false);
}

bool PollTerrainGround(FTerrainWorldState& State)
{
	FTerrainLoadingState& Loading = State.Loading;
	const std::optional<terrain::unreal::GroundLevel> Level = Loading.Ground.poll();
	if (!Level.has_value())
	{
		return Loading.Ground.pending();
	}
	Loading.KnownGround = *Level;
	APawn* Pawn = Loading.PawnToPlace.Get();
	Loading.PawnToPlace.Reset();
	const terrain::unreal::UnrealPoint Stand =
		terrain::unreal::toUnreal(State.Frame, {Level->x, Level->stand, Level->z});
	if (Pawn != nullptr)
	{
		const double AboveCm = Pawn->GetSimpleCollisionHalfHeight() + LoadClearanceCm;
		Pawn->SetActorLocation(FVector(Stand.x, Stand.y, Stand.z + AboveCm), false, nullptr,
			ETeleportType::TeleportPhysics);
	}
	const FString Water = Level->stand > Level->ground
		? FString::Printf(TEXT(", the water over it at %.2f m"), Level->stand)
		: FString();
	UE_LOG(LogTerrainEngine, Log,
		TEXT("Terrain World: the ground of (%.1f, %.1f) m read on a thread in %.1f ms: %.2f m%s; "
			 "the pawn placed over %.2f m"),
		Level->x, Level->z, Level->milliseconds, Level->ground, *Water, Level->stand);
	return false;
}

double TerrainLevelZ(const FTerrainWorldState& State, const FVector& Location, bool bStand)
{
	const terrain::unreal::CorePoint Core =
		terrain::unreal::toCore(State.Frame, ToUnrealPoint(Location));
	const TOptional<terrain::unreal::GroundLevel>& Known = State.Loading.KnownGround;
	const bool bKnown = Known.IsSet() && FMath::Abs(Known->x - Core.x) < LoadKnownGroundMeters &&
		FMath::Abs(Known->z - Core.z) < LoadKnownGroundMeters;
	const terrain::unreal::GroundLevel Level = bKnown ? *Known
		: terrain::unreal::groundLevelAt(State.Session.tiles().heightfield(), Core.x, Core.z);
	return terrain::unreal::toUnreal(
		State.Frame, {Core.x, bStand ? Level.stand : Level.ground, Core.z}).z;
}

bool ATerrainWorld::TeleportWhenReady(APawn* Pawn, double CoreX, double CoreZ,
	float HeadingDegrees, float PitchDegrees)
{
	if (!State.IsValid() || Pawn == nullptr)
	{
		UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain World: no streaming or no pawn to teleport"));
		return false;
	}
	FTerrainWorldState& Terrain = *State.Get();
	if (Terrain.Loading.Phase.IsSet())
	{
		Terrain.Loading.Phase->release();
	}
	// Black at once: nothing of the new place shows before its ground.
	SetLoadBlack(*this, Terrain, LoadingSettings.bFade);
	// The pawn at the point at once, held, its height kept: the ground there is read on a thread
	// (202 ms measured the first time in a region), the pawn placed over it, or over the water,
	// once read (PollTerrainGround).
	const double Begin = FPlatformTime::Seconds();
	const FVector Point = LoadFocusPoint(Terrain, CoreX, CoreZ);
	const FVector Location(Point.X, Point.Y, Pawn->GetActorLocation().Z);
	Pawn->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	HoldLoadPawn(Terrain, Pawn, true);
	const double Moved = FPlatformTime::Seconds();
	const terrain::unreal::ViewAngles Angles =
		terrain::unreal::viewAngles(HeadingDegrees, PitchDegrees);
	if (AController* Controller = Pawn->GetController())
	{
		Controller->SetControlRotation(FRotator(Angles.pitch, Angles.yaw, 0.0));
	}
	UE_LOG(LogTerrainEngine, Log,
		TEXT("Terrain World: %s teleported to (%.1f, %.1f) m, heading %.1f (yaw %.1f), pitch "
			 "%.1f; held until the world around is shown"),
		*Pawn->GetName(), CoreX, CoreZ, HeadingDegrees, Angles.yaw, Angles.pitch);
	BeginTerrainLoad(Terrain, Location, terrain::unreal::LoadReason::Teleport);
	Terrain.Loading.Ground.ask(Terrain.Session.tiles().heightfield(), CoreX, CoreZ);
	Terrain.Loading.PawnToPlace = Pawn;
	const double End = FPlatformTime::Seconds();
	UE_LOG(LogTerrainEngine, Log,
		TEXT("Terrain World: the teleport took %.1f ms on the game thread: the move of the pawn "
			 "%.1f ms, the report of the previous load and the start of this one %.1f ms; the "
			 "ground of the point is read on a thread"),
		(End - Begin) * 1000.0, (Moved - Begin) * 1000.0, (End - Moved) * 1000.0);
	return true;
}

bool ATerrainWorld::PrepareLocation(double CoreX, double CoreZ)
{
	if (!State.IsValid() || !State->Loading.Phase.IsSet())
	{
		return false;
	}
	BeginTerrainLoad(*State.Get(), LoadFocusPoint(*State.Get(), CoreX, CoreZ),
		terrain::unreal::LoadReason::Prepared);
	State->Loading.Phase->prepare(CoreX, CoreZ, FPlatformTime::Seconds());
	UE_LOG(LogTerrainEngine, Log, TEXT("Terrain World: preparing the point (%.1f, %.1f) m"), CoreX,
		CoreZ);
	return true;
}

void ATerrainWorld::ReleasePreparedLocation()
{
	if (!State.IsValid() || !State->Loading.Phase.IsSet() ||
		!State->Loading.Phase->prepared().has_value())
	{
		return;
	}
	const auto [X, Z] = *State->Loading.Phase->prepared();
	State->Loading.Phase->release();
	FVector Focus = FVector::ZeroVector;
	APawn* Pawn = nullptr;
	if (!FindFocus(Focus, Pawn))
	{
		return;
	}
	// Back to the player: far from the point, its place loads again as after a teleport.
	const terrain::ChunkCoord Chunk = LoadCanonicalChunk(*State.Get(), Focus);
	State->Loading.LastChunk = Chunk;
	const terrain::ChunkCoord Point = terrain::wrapChunk(terrain::worldToChunk(X, Z),
		State->Session.tiles().world());
	if (terrain::unreal::wrappedChunkDistance(Point, Chunk, State->Session.tiles().world()) >
		ViewDistanceChunks)
	{
		BeginTerrainLoad(*State.Get(), Focus, terrain::unreal::LoadReason::Teleport);
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
	return TeleportWhenReady(Pawn, CoreX, CoreZ, HeadingDegrees, PitchDegrees);
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
			 "heading (0 north, 90 east) and the pitch (negative: down), the screen black and the "
			 "pawn held until the world around is shown, and measures the load (log and CSV in "
			 "Saved/TerrainEngine/Loading): Terrain.LoadTest x z [heading] [pitch]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunTerrainLoadTest));
} // namespace

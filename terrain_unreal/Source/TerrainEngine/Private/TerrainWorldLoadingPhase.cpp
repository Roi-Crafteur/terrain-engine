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
#include "TerrainWorldState.h"

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

	// The Unreal position AboveCm over the ground of the core point (x, z).
	FVector LoadGroundPoint(const FTerrainWorldState& State, double X, double Z, double AboveCm)
	{
		const double Ground = State.Session.tiles().heightfield().heightAt(X, Z);
		const terrain::unreal::UnrealPoint Point =
			terrain::unreal::toUnreal(State.Frame, {X, Ground, Z});
		return FVector(Point.x, Point.y, Point.z + AboveCm);
	}

	APlayerCameraManager* LoadCamera(const ATerrainWorld& World)
	{
		APlayerController* Controller = UGameplayStatics::GetPlayerController(&World, 0);
		return Controller != nullptr ? Controller->PlayerCameraManager.Get() : nullptr;
	}

	// The screen black, or coming back from black.
	void SetLoadBlack(const ATerrainWorld& World, FTerrainWorldState& State, bool bBlack)
	{
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

	// The pawn held still (its movement off: no gravity) or given back, placed on the ground if it
	// stands below it.
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
		const double GroundZ = State.Session.groundZ(State.Frame, ToUnrealPoint(Location));
		const double HalfHeight = Held->GetSimpleCollisionHalfHeight();
		if (Location.Z - HalfHeight < GroundZ)
		{
			const FVector OnGround(Location.X, Location.Y, GroundZ + HalfHeight + LoadClearanceCm);
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
		const terrain::unreal::LoadBudget Budget =
			Loading.Phase->budget(bPending, Loading.MeanUploadMilliseconds);
		if (Budget == Loading.Applied)
		{
			return;
		}
		State.Session.streamer().setWorkerLimit(Budget.workers);
		State.Session.streamer().setMaxLoadsPerUpdate(Budget.uploadsPerFrame);
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
		return LoadGroundPoint(State, X, Z, 0.0);
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
	if (Uploads.Count > 0)
	{
		Loading.MeanUploadMilliseconds =
			0.8 * Loading.MeanUploadMilliseconds + 0.2 * Uploads.Milliseconds / Uploads.Count;
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
	const double AboveCm = Pawn->GetSimpleCollisionHalfHeight() + LoadClearanceCm;
	const FVector Location = LoadGroundPoint(Terrain, CoreX, CoreZ, AboveCm);
	Pawn->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	HoldLoadPawn(Terrain, Pawn, true);
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
	return true;
}

bool ATerrainWorld::PrepareLocation(double CoreX, double CoreZ)
{
	if (!State.IsValid() || !State->Loading.Phase.IsSet())
	{
		return false;
	}
	BeginTerrainLoad(*State.Get(), LoadGroundPoint(*State.Get(), CoreX, CoreZ, 0.0),
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

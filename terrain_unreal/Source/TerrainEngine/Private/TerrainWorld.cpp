#include "TerrainWorld.h"

#include "Bridge/determinism_check.hpp"
#include "Bridge/terrain_session.hpp"
#include "Bridge/tile_streamer.hpp"
#include "Bridge/tile_timings.hpp"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "TerrainEngineLog.h"
#include "TerrainMeshBackend.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>

// The terrain_core side of a playing ATerrainWorld: its session (tiles, timings, streamer) and
// the frame placing the core world in the Unreal world.
struct FTerrainWorldState
{
	explicit FTerrainWorldState(const terrain::unreal::SessionParams& Params)
		: Session(Params)
	{
	}

	terrain::unreal::TerrainSession Session;
	// Fixed while playing, rebasing aside: the player keeps unwrapped coordinates across the edges
	// of the wrapped world, the tiles follow it to their nearest images (Bridge/tile_streamer.hpp).
	terrain::unreal::UnrealFrame Frame;
};

namespace
{
	// Above the ground, the bottom of a pawn placed on it, centimeters.
	constexpr double SnapClearanceCm = 50.0;

	// Created by terrain_unreal/Scripts/create_terrain_material.py.
	const TCHAR* const GeneratedMaterialPath =
		TEXT("/Game/TerrainEngine/M_TerrainAltitude.M_TerrainAltitude");

	terrain::unreal::UnrealPoint ToUnrealPoint(const FVector& Vector)
	{
		return {Vector.X, Vector.Y, Vector.Z};
	}

	FString ToFString(const std::string& Text)
	{
		return FString(UTF8_TO_TCHAR(Text.c_str()));
	}

	terrain::unreal::SessionParams MakeSessionParams(const ATerrainWorld& World)
	{
		terrain::unreal::SessionParams Params;
		Params.seed = static_cast<std::uint64_t>(World.Seed);
		Params.caves = World.bCaves;
		Params.caveRegions = World.bCaves && World.bCaveRegions;
		Params.stream.lod.splitDistance = {
			World.Lod0DistanceChunks, World.Lod1DistanceChunks, World.Lod2DistanceChunks};
		Params.stream.lod.viewDistance = World.ViewDistanceChunks;
		Params.stream.workerThreads = FMath::Max(1, World.WorkerThreads);
		Params.stream.maxLoadsPerUpdate =
			static_cast<std::size_t>(FMath::Max(1, World.MaxTileUploadsPerFrame));
		return Params;
	}

	// Applies an update of the streamer to the meshes: the uploads (timed), the moves to other
	// images of the wrapped world, then the removals.
	void ApplyUpdate(UTerrainMeshBackend& Backend, terrain::unreal::TileTimings& Timings,
		const terrain::unreal::StreamerUpdate& Update)
	{
		for (const terrain::unreal::TileUpload& Upload : Update.uploads)
		{
			const double Start = FPlatformTime::Seconds();
			Backend.SetTile(Upload);
			const double UploadMs = (FPlatformTime::Seconds() - Start) * 1000.0;
			Timings.record(terrain::unreal::TileStage::Upload, Upload.key.lod, UploadMs);
			UE_LOG(LogTerrainEngine, Verbose,
				TEXT("Tile (%d, %d) LOD %d: %d vertices, %d triangles, collision %s, conversion "
					 "%.2f ms, upload %.2f ms"),
				Upload.key.origin.x, Upload.key.origin.z, Upload.key.lod,
				static_cast<int32>(Upload.mesh.positions.size()),
				static_cast<int32>(Upload.mesh.triangleCount()),
				Upload.collision ? TEXT("yes") : TEXT("no"), Upload.conversionMilliseconds,
				UploadMs);
		}
		for (const terrain::unreal::TileMove& Move : Update.moves)
		{
			Backend.MoveTile(Move.key, Move.origin);
		}
		for (const terrain::TileKey& Key : Update.removals)
		{
			Backend.RemoveTile(Key);
		}
	}
} // namespace

ATerrainWorld::ATerrainWorld()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
#if WITH_EDITORONLY_DATA
	// World Partition must not unload the actor that streams the terrain.
	bIsSpatiallyLoaded = false;
#endif
}

void ATerrainWorld::BeginPlay()
{
	Super::BeginPlay();
	if (!StartStreaming())
	{
		SetActorTickEnabled(false);
	}
}

void ATerrainWorld::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Backend != nullptr)
	{
		Backend->RemoveAllTiles();
		Backend = nullptr;
	}
	State.Reset(); // the workers stop once their current tile is generated
	WarmedPawn.Reset();
	Super::EndPlay(EndPlayReason);
}

bool ATerrainWorld::StartStreaming()
{
	try
	{
		// The world must be the one of terrain_core, bit for bit: otherwise no streaming.
		const terrain::unreal::DeterminismReport Check = terrain::unreal::checkDefaultWorld();
		if (!Check.same)
		{
			UE_LOG(LogTerrainEngine, Error, TEXT("Terrain World: %s. No streaming."),
				*ToFString(terrain::unreal::describe(Check)));
			return false;
		}
		UE_LOG(LogTerrainEngine, Log, TEXT("Terrain World: %s"),
			*ToFString(terrain::unreal::describe(Check)));
		State = MakePimpl<FTerrainWorldState>(MakeSessionParams(*this));
		State->Frame = State->Session.frameAt(CoreOriginX, CoreOriginZ);
	}
	catch (const std::exception& Exception)
	{
		State.Reset();
		UE_LOG(LogTerrainEngine, Error, TEXT("Terrain World: cannot start the streaming: %s"),
			*ToFString(Exception.what()));
		return false;
	}
	Backend = CreateTerrainMeshBackend(MeshType, *this, ResolveMaterial(), bAsyncCollisionCooking);
	UE_LOG(LogTerrainEngine, Log,
		TEXT("Terrain World: seed %lld, ground of (%.1f, %.1f) m at %.2f m placed at the origin, "
			 "LOD rings %d/%d/%d/%d chunks, %d workers, %d uploads per frame"),
		Seed, CoreOriginX, CoreOriginZ, State->Frame.origin.y, Lod0DistanceChunks,
		Lod1DistanceChunks, Lod2DistanceChunks, ViewDistanceChunks, WorkerThreads,
		MaxTileUploadsPerFrame);
	return true;
}

void ATerrainWorld::StopStreaming(const FString& Reason)
{
	UE_LOG(LogTerrainEngine, Error, TEXT("Terrain World: streaming stopped: %s"), *Reason);
	SetActorTickEnabled(false);
}

void ATerrainWorld::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!State.IsValid() || Backend == nullptr)
	{
		return;
	}
	FVector Focus = FVector::ZeroVector;
	APawn* Pawn = nullptr;
	if (!FindFocus(Focus, Pawn))
	{
		return;
	}
	if (AActor* Traveller = FocusActor != nullptr ? FocusActor.Get() : Pawn)
	{
		ReturnFromWorldTurn(*Traveller, Focus);
	}
	try
	{
		if (Pawn != nullptr && WarmedPawn.Get() != Pawn)
		{
			WarmUpAround(*Pawn);
		}
		ApplyUpdate(*Backend, State->Session.timings(),
			State->Session.streamer().update(ToUnrealPoint(Focus), State->Frame));
	}
	catch (const std::exception& Exception)
	{
		StopStreaming(ToFString(Exception.what()));
		return;
	}
	LogTimings(DeltaSeconds);
}

void ATerrainWorld::ApplyWorldOffset(const FVector& InOffset, bool bWorldShift)
{
	Super::ApplyWorldOffset(InOffset, bWorldShift);
	// Unreal moved every actor and component by InOffset: the tiles to come must stand next to
	// the tiles it moved.
	if (State.IsValid())
	{
		State->Frame = terrain::unreal::shifted(State->Frame, ToUnrealPoint(InOffset));
	}
}

bool ATerrainWorld::FindFocus(FVector& OutLocation, APawn*& OutPawn) const
{
	if (FocusActor != nullptr)
	{
		OutLocation = FocusActor->GetActorLocation();
		OutPawn = Cast<APawn>(FocusActor.Get());
		return true;
	}
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		OutLocation = PlayerPawn->GetActorLocation();
		OutPawn = PlayerPawn;
		return true;
	}
	if (const APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0))
	{
		FRotator Rotation;
		Controller->GetPlayerViewPoint(OutLocation, Rotation);
		OutPawn = nullptr;
		return true;
	}
	return false;
}

// Far from the origin, after most of a turn of the wrapped world, the traveller is moved back by
// whole worlds (Bridge/unreal_frame.hpp: worldTurnOffset): the same place of the core, so the
// update of this tick moves every tile by the same offset and nothing changes on screen. A
// teleport keeps the velocity of its physics and of its movement component; the frame stays.
void ATerrainWorld::ReturnFromWorldTurn(AActor& Traveller, FVector& InOutFocus) const
{
	const terrain::unreal::UnrealPoint Offset = terrain::unreal::worldTurnOffset(
		ToUnrealPoint(InOutFocus), State->Session.streamer().manager().params().world);
	if (Offset.x == 0.0 && Offset.y == 0.0)
	{
		return;
	}
	const FVector Shift(Offset.x, Offset.y, Offset.z);
	Traveller.SetActorLocation(
		Traveller.GetActorLocation() + Shift, false, nullptr, ETeleportType::TeleportPhysics);
	InOutFocus += Shift;
	UE_LOG(LogTerrainEngine, Log,
		TEXT("Terrain World: %s moved back by whole worlds (%.0f, %.0f) km, now at "
			 "(%.0f, %.0f) km"),
		*Traveller.GetName(), Shift.X / 1.0e5, Shift.Y / 1.0e5, InOutFocus.X / 1.0e5,
		InOutFocus.Y / 1.0e5);
}

// The ground under a new pawn, before it can fall: the LOD 0 tiles around it are generated now.
void ATerrainWorld::WarmUpAround(APawn& Pawn)
{
	WarmedPawn = &Pawn;
	const double Start = FPlatformTime::Seconds();
	const std::chrono::milliseconds Timeout(FMath::RoundToInt(WarmUpTimeoutSeconds * 1000.0f));
	const terrain::unreal::WarmUpResult Warm = State->Session.streamer().warmUp(
		ToUnrealPoint(Pawn.GetActorLocation()), State->Frame, WarmUpRadiusChunks, Timeout);
	for (const terrain::unreal::StreamerUpdate& Update : Warm.updates)
	{
		ApplyUpdate(*Backend, State->Session.timings(), Update);
	}
	const double Seconds = FPlatformTime::Seconds() - Start;
	if (Warm.complete)
	{
		UE_LOG(LogTerrainEngine, Log, TEXT("Terrain World: ground ready under %s in %.2f s"),
			*Pawn.GetName(), Seconds);
	}
	else
	{
		UE_LOG(LogTerrainEngine, Warning,
			TEXT("Terrain World: the ground under %s was not ready after %.2f s"),
			*Pawn.GetName(), Seconds);
	}
	if (bSnapPawnToGround)
	{
		SnapToGround(Pawn);
	}
}

// Places a pawn standing below the ground on it; a pawn above it falls onto it.
void ATerrainWorld::SnapToGround(APawn& Pawn) const
{
	const FVector Location = Pawn.GetActorLocation();
	const double GroundZ = State->Session.groundZ(State->Frame, ToUnrealPoint(Location));
	const double HalfHeight = Pawn.GetSimpleCollisionHalfHeight();
	if (Location.Z - HalfHeight >= GroundZ)
	{
		return;
	}
	const FVector OnGround(Location.X, Location.Y, GroundZ + HalfHeight + SnapClearanceCm);
	Pawn.SetActorLocation(OnGround, false, nullptr, ETeleportType::TeleportPhysics);
	if (UPawnMovementComponent* Movement = Pawn.GetMovementComponent())
	{
		Movement->StopMovementImmediately();
	}
	UE_LOG(LogTerrainEngine, Log, TEXT("Terrain World: %s placed on the ground at Z = %.0f cm"),
		*Pawn.GetName(), GroundZ);
}

void ATerrainWorld::LogTimings(float DeltaSeconds)
{
	SecondsSinceTimingLog += DeltaSeconds;
	if (TimingLogIntervalSeconds <= 0.0f || SecondsSinceTimingLog < TimingLogIntervalSeconds)
	{
		return;
	}
	SecondsSinceTimingLog = 0.0f;
	const terrain::unreal::TileStreamer& Streamer = State->Session.streamer();
	UE_LOG(LogTerrainEngine, Log, TEXT("Terrain World: %d tiles shown, %d pending"),
		static_cast<int32>(Streamer.shownCount()),
		static_cast<int32>(Streamer.manager().pendingCount()));
	for (const std::string& Line : terrain::unreal::describe(State->Session.timings().take()))
	{
		UE_LOG(LogTerrainEngine, Log, TEXT("  %s"), *ToFString(Line));
	}
}

UMaterialInterface* ATerrainWorld::ResolveMaterial() const
{
	if (Material != nullptr)
	{
		return Material;
	}
	if (UMaterialInterface* Generated = LoadObject<UMaterialInterface>(
			nullptr, GeneratedMaterialPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		return Generated;
	}
	UE_LOG(LogTerrainEngine, Log,
		TEXT("Terrain World: no material set and no %s (terrain_unreal/Scripts/"
			 "create_terrain_material.py creates it): the vertex colors are shown by the debug "
			 "material of Unreal"),
		GeneratedMaterialPath);
	if (GEngine == nullptr)
	{
		return nullptr;
	}
	UMaterial* VertexColors = GEngine->VertexColorMaterial;
	return VertexColors;
}

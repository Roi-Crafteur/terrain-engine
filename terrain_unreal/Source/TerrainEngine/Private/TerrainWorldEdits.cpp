// Player edits of ATerrainWorld (phase 15), to try them before the gameplay breaks and places
// blocks: ApplyVolumeEdit and EditAhead, and the console command Terrain.Edit. The session
// regenerates the tiles over an edit and stages it for the far horizon, whose next epoch sends
// the bands around it (Bridge/terrain_session.hpp, terrain_core streaming/horizon_edits.hpp).
#include "TerrainWorld.h"

#include "Bridge/terrain_edits.hpp"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "TerrainEngineLog.h"
#include "TerrainWorldState.h"

#include "terrain/storage/volume_edit.hpp"

#include <cmath>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <vector>

namespace
{
	using TerrainWorldConversions::ToFString;
	using TerrainWorldConversions::ToUnrealPoint;

	// Applies an edit to the session, logged with its cost: the chunks it changed, 0 on failure.
	int32 ApplyToSession(FTerrainWorldState* State, const terrain::VolumeEdit& Edit,
		const TCHAR* What)
	{
		if (State == nullptr)
		{
			UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain World: no edit (%s) outside play"),
				What);
			return 0;
		}
		const double Start = FPlatformTime::Seconds();
		try
		{
			const std::vector<terrain::ChunkCoord> Chunks = State->Session.applyEdit(Edit);
			UE_LOG(LogTerrainEngine, Log,
				TEXT("Terrain World: %s at (%d, %d, %d): %d chunks to generate again, staged for "
					 "the far horizon in %.1f ms"),
				What, Edit.anchor.x, Edit.anchor.y, Edit.anchor.z,
				static_cast<int32>(Chunks.size()), (FPlatformTime::Seconds() - Start) * 1000.0);
			return static_cast<int32>(Chunks.size());
		}
		catch (const std::exception& Exception)
		{
			UE_LOG(LogTerrainEngine, Error, TEXT("Terrain World: %s refused: %s"), What,
				*ToFString(Exception.what()));
			return 0;
		}
	}

	constexpr int64 MaxUint16 = 65535;

	int32 FloorToBlock(double Meters)
	{
		return static_cast<int32>(std::floor(Meters));
	}

	// Whole blocks (or sixteenths of a meter for a radius), at least 1.
	uint16 ToBlocks(double Value, int64 Most)
	{
		const int64 Rounded = std::isfinite(Value) ? std::llround(Value) : 1;
		return static_cast<uint16>(FMath::Clamp<int64>(Rounded, 1, Most));
	}

	// Terrain.Edit raze <distance> <side> [depth] | blast <distance> <radius> | heap <distance>
	// <radius>, meters: on every Terrain World of the world.
	void RunTerrainEdit(const TArray<FString>& Args, UWorld* World)
	{
		if (World == nullptr || Args.Num() < 3)
		{
			UE_LOG(LogTerrainEngine, Warning,
				TEXT("Terrain.Edit raze <distance m> <side m> [depth m] | blast <distance m> "
					 "<radius m> | heap <distance m> <radius m>"));
			return;
		}
		const float Depth = Args.Num() > 3 ? FCString::Atof(*Args[3]) : 10.0f;
		for (TActorIterator<ATerrainWorld> It(World); It; ++It)
		{
			It->EditAhead(Args[0], FCString::Atof(*Args[1]), FCString::Atof(*Args[2]), Depth);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs TerrainEditCommand(TEXT("Terrain.Edit"),
		TEXT("Edits the ground ahead of the camera, the far horizon following: Terrain.Edit raze "
			 "<distance m> <side m> [depth m] | blast <distance m> <radius m> | heap <distance m> "
			 "<radius m>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunTerrainEdit));
} // namespace

int32 ATerrainWorld::ApplyVolumeEdit(ETerrainEditShape Shape, ETerrainEditMode Mode,
	FVector Center, FVector SizeMeters, float RadiusMeters)
{
	if (!State.IsValid())
	{
		return ApplyToSession(nullptr, {}, TEXT("ApplyVolumeEdit"));
	}
	const terrain::unreal::CorePoint Core = terrain::unreal::toCore(State->Frame,
		ToUnrealPoint(Center));
	terrain::VolumeEdit Edit;
	Edit.mode = terrain::EditMode::Remove;
	if (Mode != ETerrainEditMode::Remove)
	{
		Edit.mode = Mode == ETerrainEditMode::Add ? terrain::EditMode::Add
												  : terrain::EditMode::Restore;
	}
	if (Shape == ETerrainEditShape::Box)
	{
		// Unreal X, Y and Z sizes are the core x, z and y ones.
		Edit.shape = terrain::EditShape::Box;
		Edit.size = {ToBlocks(SizeMeters.X, MaxUint16), ToBlocks(SizeMeters.Z, MaxUint16),
			ToBlocks(SizeMeters.Y, MaxUint16)};
		Edit.anchor = {FloorToBlock(Core.x - SizeMeters.X / 2.0),
			FloorToBlock(Core.y - SizeMeters.Z / 2.0), FloorToBlock(Core.z - SizeMeters.Y / 2.0)};
	}
	else
	{
		Edit.shape = Shape == ETerrainEditShape::Explosion ? terrain::EditShape::Explosion
														   : terrain::EditShape::Sphere;
		Edit.radius = ToBlocks(RadiusMeters * 16.0, MaxUint16);
		Edit.anchor = {FloorToBlock(Core.x), FloorToBlock(Core.y), FloorToBlock(Core.z)};
	}
	return ApplyToSession(State.Get(), Edit, TEXT("ApplyVolumeEdit"));
}

bool ATerrainWorld::EditAhead(const FString& Kind, float DistanceMeters, float SizeMeters,
	float DepthMeters)
{
	std::vector<std::string> Args = {TCHAR_TO_UTF8(*Kind.ToLower()),
		std::to_string(DistanceMeters), std::to_string(SizeMeters)};
	if (Args[0] == "raze")
	{
		Args.push_back(std::to_string(DepthMeters));
	}
	std::string Error;
	const std::optional<terrain::unreal::GroundEditCommand> Command =
		terrain::unreal::parseGroundEdit(Args, Error);
	APlayerController* Controller = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!Command || !State.IsValid() || Controller == nullptr ||
		Controller->PlayerCameraManager == nullptr)
	{
		UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain World: no edit ahead: %s"),
			Command ? TEXT("no camera, or not playing") : *ToFString(Error));
		return false;
	}
	const FVector Camera = Controller->PlayerCameraManager->GetCameraLocation();
	const FVector Forward = Controller->PlayerCameraManager->GetCameraRotation().Vector();
	const terrain::unreal::CorePoint Target = terrain::unreal::pointAhead(State->Frame,
		ToUnrealPoint(Camera), Forward.X, Forward.Y, Command->distance);
	const terrain::VolumeEdit Edit = terrain::unreal::groundEditAt(
		State->Session.tiles().heightfield(), *Command, Target.x, Target.z);
	const FString What = FString::Printf(TEXT("%s of %.0f m %.0f m ahead"), *Kind.ToLower(),
		Command->size, Command->distance);
	return ApplyToSession(State.Get(), Edit, *What) > 0;
}

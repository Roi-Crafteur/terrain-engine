// Terrain.EditTiles <x> <z> (agent 41), in the tools of the developers only: never in a Shipping
// build. What the tiles over a point of the core hold, in the core and in the meshes of Unreal
// (Bridge/edit_tiles_report.hpp): first the point the streaming follows, the camera and the pawn
// possessed, then one line per tile with the ground of the column of the point in the data of the
// core and in the section its mesh received, its LOD and the LOD from the camera, its bounds,
// whether it is visible and when it was uploaded. For the edits not seen in Unreal after a
// Terrain.Goto (the second test of #78), which the streaming of the bridge shows.
#include "CoreMinimal.h"

#if TERRAIN_DEV_TOOLS

#include "Bridge/dev_commands.hpp"
#include "Bridge/edit_tiles_report.hpp"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TerrainEngineLog.h"
#include "TerrainMeshBackend.h"
#include "TerrainWorld.h"
#include "TerrainWorldBuild.h"
#include "TerrainWorldState.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
	void LogLine(const std::string& Line)
	{
		UE_LOG(LogTerrainEngine, Log, TEXT("Terrain dev: %s"), UTF8_TO_TCHAR(Line.c_str()));
	}

	terrain::unreal::UnrealPoint ToPoint(const FVector& Point)
	{
		return {Point.X, Point.Y, Point.Z};
	}

	// The point the tiles are streamed around (the last frame), the camera and the pawn possessed.
	terrain::unreal::ViewPositions Positions(ATerrainWorld& Terrain, const FTerrainWorldState& State)
	{
		terrain::unreal::ViewPositions View;
		View.focus = ToPoint(State.Loading.StreamingFocus);
		View.camera = View.focus;
		View.focusActor = Terrain.FocusActor != nullptr;
		APlayerController* Controller = Terrain.GetWorld()->GetFirstPlayerController();
		if (Controller != nullptr && Controller->PlayerCameraManager != nullptr)
		{
			View.camera = ToPoint(Controller->PlayerCameraManager->GetCameraLocation());
		}
		if (APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr)
		{
			View.pawn = ToPoint(Pawn->GetActorLocation());
			View.pawnClass = TCHAR_TO_UTF8(*Pawn->GetClass()->GetName());
		}
		return View;
	}

	// The tiles the streamer shows over the point, then the meshes of Unreal over it that it
	// does not show.
	std::vector<terrain::TileKey> TilesOver(const terrain::unreal::TileStreamer& Streamer,
		const UTerrainMeshBackend* Backend, double X, double Z)
	{
		std::vector<terrain::TileKey> Keys = terrain::unreal::tilesOver(Streamer, X, Z);
		if (Backend == nullptr)
		{
			return Keys;
		}
		for (const terrain::TileKey& Key : Backend->GetTileKeys())
		{
			if (!Streamer.shown(Key) && terrain::unreal::tileHolds(Key, X, Z, 1.0) &&
				std::find(Keys.begin(), Keys.end(), Key) == Keys.end())
			{
				Keys.push_back(Key);
			}
		}
		return Keys;
	}

	void EditTiles(ATerrainWorld& Terrain, FTerrainWorldState& State, double X, double Z)
	{
		const terrain::unreal::ViewPositions View = Positions(Terrain, State);
		LogLine(terrain::unreal::positionsLine(State.Frame, X, Z, View));
		const terrain::unreal::CorePoint Camera = terrain::unreal::toCore(State.Frame, View.camera);
		const terrain::ChunkCoord CameraChunk = terrain::worldToChunk(Camera.x, Camera.z);
		const terrain::unreal::TileStreamer& Streamer = State.Session.streamer();
		UTerrainMeshBackend* Backend = FTerrainBuildHooks::Backend(Terrain);
		const std::vector<terrain::TileKey> Keys = TilesOver(Streamer, Backend, X, Z);
		if (Keys.empty())
		{
			UE_LOG(LogTerrainEngine, Log, TEXT("Terrain dev: EditTiles (%.2f, %.2f): no tile over "
				"the point"), X, Z);
		}
		for (const terrain::TileKey& Key : Keys)
		{
			std::optional<terrain::unreal::ShownSection> Shown;
			terrain::unreal::ShownSection Section;
			if (Backend != nullptr && Backend->DescribeTile(Key, Section))
			{
				Shown = std::move(Section);
			}
			LogLine(terrain::unreal::editTilesLine(X, Z, terrain::unreal::reportTile(Streamer,
				State.Frame, Key, X, Z, CameraChunk, Shown)));
		}
	}

	void RunEditTiles(const TArray<FString>& Args, UWorld* World)
	{
		std::vector<std::string> Utf8;
		for (const FString& Arg : Args)
		{
			Utf8.emplace_back(TCHAR_TO_UTF8(*Arg));
		}
		std::string Error;
		const std::optional<terrain::unreal::EditTilesCommand> Command =
			terrain::unreal::parseEditTiles(Utf8, Error);
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
				EditTiles(**It, *State, Command->x, Command->z);
			}
		}
	}

	FAutoConsoleCommandWithWorldAndArgs EditTilesCommand(TEXT("Terrain.EditTiles"),
		TEXT("The tiles over a point of the core: Terrain.EditTiles <x> <z>; the ground of its "
			 "column in the core and in the meshes of Unreal, their LOD, bounds and uploads"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunEditTiles));
} // namespace

#endif // TERRAIN_DEV_TOOLS

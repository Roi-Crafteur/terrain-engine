#pragma once

#include "CoreMinimal.h"

#include "terrain/chunk_coord.hpp"

class APawn;
class ATerrainWorld;
struct FTerrainWorldState;

// The measures of the loads of ATerrainWorld (agent 38, TerrainWorldLoading.cpp): when the player
// appears (the first frame with a focus) and after each teleport (a jump farther than the view of
// the tiles in one frame, or Terrain.LoadTest), the timeline of the session
// (Bridge/load_timeline.hpp) logs each milestone as it is reached, then the summary and the curve
// "distance -> instant shown", and writes the CSV of the tiles into Saved/TerrainEngine/Loading/.
struct FTerrainLoadingState
{
	// The durations of the start of the streaming (BeginPlay), seconds.
	double DeterminismSeconds = 0.0;
	double SessionSeconds = 0.0; // the planet level, the hydrology, the tiles and their workers
	double HorizonSeconds = 0.0; // the horizon, the water and the sky
	// The chunk of the focus at the last frame (canonical), to detect the teleports.
	bool bHasLastChunk = false;
	terrain::ChunkCoord LastChunk;
	// The loads measured since the start, and whether the report of the current one is written.
	int32 Loads = 0;
	bool bReported = true;
};

// Before the tiles are streamed in a frame: starts a load at the first focus (the spawn) or after a
// jump of the focus (a teleport).
void NoteTerrainFocus(ATerrainWorld& World, FTerrainWorldState& State, const FVector& Focus);

// Starts the measure of a load now (a teleport by Terrain.LoadTest, which forces it).
void BeginTerrainLoad(FTerrainWorldState& State, const FVector& Focus, bool bTeleport);

// After the tiles and the horizon of a frame: the milestones reached (logged), the collision under
// the pawn, the frame; once the load is complete or too old, its report and CSV.
void TickTerrainLoading(ATerrainWorld& World, FTerrainWorldState& State, const FVector& Focus,
	APawn* Pawn, int32 Uploads);

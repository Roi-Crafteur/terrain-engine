#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include "Bridge/determinism_check.hpp"
#include "Bridge/load_phase.hpp"
#include "Bridge/load_timeline.hpp"

#include "terrain/chunk_coord.hpp"

#include <future>

class APawn;
class ATerrainWorld;
struct FTerrainWorldState;

// The loads of ATerrainWorld (agent 38, TerrainWorldLoading.cpp). When the player appears (the
// first frame with a focus), after each teleport (a jump farther than the view of the tiles in one
// frame, TeleportWhenReady, Terrain.LoadTest) and around a prepared point (PrepareLocation):
// - the phase of the load (Bridge/load_phase.hpp): more tile workers, horizon pieces and uploads
//   within a cap, the screen held black until the ground, the 64 m ring, every tile and the levels
//   0 to 3 of the horizon are shown (LoadingFadeMaxSeconds at most, what was missing logged), the
//   pawn held until the collision under it exists;
// - the measures (Bridge/load_timeline.hpp): each milestone logged as it is reached, then the
//   summary, the curve "distance -> instant shown" and the CSV of the tiles in
//   Saved/TerrainEngine/Loading/.
struct FTerrainLoadingState
{
	// The durations of the start of the streaming (BeginPlay), seconds.
	double SessionSeconds = 0.0; // the planet level, the hydrology, the tiles and their workers
	double HorizonSeconds = 0.0; // the horizon, the water and the sky
	// The check of the build (determinism_check.hpp), run on a thread from the start.
	std::future<terrain::unreal::DeterminismReport> Determinism;
	double DeterminismStartSeconds = 0.0;
	// The chunk of the focus at the last frame (canonical), to detect the teleports.
	bool bHasLastChunk = false;
	terrain::ChunkCoord LastChunk;
	// The loads measured since the start, and whether the report of the current one is written.
	int32 Loads = 0;
	bool bReported = true;
	// The phase of the loads, its budget applied last, the mean cost of an upload (ms).
	TOptional<terrain::unreal::LoadPhase> Phase;
	terrain::unreal::LoadBudget Applied;
	double MeanUploadMilliseconds = 0.1;
	bool bScreenBlack = false;      // the fade holds the screen black
	bool bBoosted = false;          // the boost of the current load is on
	TWeakObjectPtr<APawn> HeldPawn; // the pawn whose movement the hold stopped
};

// The uploads of a frame and their time on the game thread.
struct FTerrainUploads
{
	int32 Count = 0;
	double Milliseconds = 0.0;
};

// The cap of the tile workers and horizon pieces together while loading: LoadingThreadCap, or
// min(hardware threads - 4, 12), at least WorkerThreads + HorizonThreads.
int32 TerrainLoadingCap(const ATerrainWorld& World);

// Once the streaming is created: the phase of the loads, and the check of the build on a thread.
void StartTerrainLoading(ATerrainWorld& World, FTerrainWorldState& State);

// False once the check of the build found another world than terrain_core's (OutError says what):
// the streaming must stop. True while it runs and once it passed (logged then).
bool PollDeterminismCheck(FTerrainWorldState& State, FString& OutError);

// Before the tiles are streamed in a frame: starts a load at the first focus (the spawn) or after
// a jump of the focus (a teleport), applies the budget of the frame, and returns the focus of the
// streaming: the prepared point while one is set, otherwise Focus.
FVector BeginTerrainFrame(ATerrainWorld& World, FTerrainWorldState& State, const FVector& Focus);

// Starts a load now around Focus: the timeline and the phase (a teleport by TeleportWhenReady,
// which forces it, or a prepared point: then the phase is the one of the point).
void BeginTerrainLoad(FTerrainWorldState& State, const FVector& Focus,
	terrain::unreal::LoadReason Reason);

// After the tiles and the horizon of a frame: the milestones (logged), the phase (the fade, the
// hold, the prepared point told ready), the frame; once the load is complete or too old, its
// report and CSV.
void TickTerrainLoading(ATerrainWorld& World, FTerrainWorldState& State, const FVector& Focus,
	APawn* Pawn, const FTerrainUploads& Uploads);

// Between TerrainWorldLoading.cpp (the measures) and TerrainWorldLoadingPhase.cpp (the phase).
void StartDeterminismCheck(FTerrainWorldState& State);
void UpdateTerrainPhase(ATerrainWorld& World, FTerrainWorldState& State, APawn* Pawn,
	const terrain::unreal::LoadStatus& Status, const FTerrainUploads& Uploads);

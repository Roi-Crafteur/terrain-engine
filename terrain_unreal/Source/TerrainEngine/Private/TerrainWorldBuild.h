#pragma once

#include "CoreMinimal.h"

#include "Bridge/edit_latency.hpp"
#include "Bridge/edit_save.hpp"
#include "Bridge/edit_thread.hpp"
#include "terrain/storage/volume_edit.hpp"

#include <cstdint>
#include <deque>
#include <memory>
#include <string>

class APawn;
class ATerrainWorld;
struct FTerrainWorldState;

// Breaking and placing in game on the side of the world (agent 28, TerrainWorldBuild.cpp): the
// edits of the players queued on the edit thread, the save of the edits of the seed, the time from
// a click to its tile shown (only with TERRAIN_DEV_TOOLS: Terrain.EditStats). A member of
// FTerrainWorldState after its session: the saver writes its last save, from the edits of the
// session, before the session goes.
struct FTerrainBuildState
{
	terrain::unreal::EditLatency Latency;
	std::deque<std::uint64_t> Clicks; // the edits of the players in the edit thread, in order
	std::unique_ptr<terrain::unreal::EditSaver> Saver;
	double LastSaveSeconds = 0.0;
	// A save refused at the start (unreadable, another seed): never written over this play.
	bool bSaveRefused = false;
};

// What ATerrainWorld gives breaking and placing (a friend of it): its hooks and its state.
struct FTerrainBuildHooks
{
	// Once the world is built (FinishStreamingStart): the saved edits of the seed read and the
	// saver started, with Building.bSaveEdits or after Terrain.EditRead 1 (once).
	static void Start(ATerrainWorld& World);
	// Terrain.EditRead (agent 41, tools of the developers): the next play of this session of the
	// editor reads and saves the edits of its seed as with Building.bSaveEdits, the actor and the
	// map untouched; the next Start takes it back to false.
	static void SetReadOnce(bool bRead);
	static bool ReadOnce();
	// Each frame: a save asked when its interval passed, the build component on the player pawn.
	static void Tick(ATerrainWorld& World, APawn* Pawn);
	// Before the world is released: a last save, written before the session goes.
	static void Stop(ATerrainWorld& World);

	// The state of a playing world, or null.
	static FTerrainWorldState* State(ATerrainWorld& World);
	// A teleport or the start holds the view (the screen black or the pawn held), in every build.
	static bool IsLoadingView(const ATerrainWorld& World);
	static bool IsDevMapOpen(const ATerrainWorld& World);
	// An edit of a player queued on the edit thread, measured from now.
	static void QueueBuild(FTerrainWorldState& State, const terrain::VolumeEdit& Edit,
		const FString& What);
	// Starts or stops the save while playing (Terrain.EditSave): false outside play.
	static bool SetSave(ATerrainWorld& World, bool bSave);
	// The light floor of the edited surfaces from Building (bEditLightFloor, EditLightFloor) to
	// the material of the tiles (TerrainWorldEditLight.cpp): false when the tiles have no
	// material instance of M_TemporaryTerrain to take it.
	static bool ApplyEditLight(ATerrainWorld& World);
};

// The edits done this frame (TickTerrainEdits): the chunks to save, the clicks measured.
void NoteBuildEdit(FTerrainWorldState& State, const terrain::unreal::EditOutcome& Outcome);
// The label of the edits of the players, before their description.
inline constexpr const char* TerrainBuildLabel = "build: ";

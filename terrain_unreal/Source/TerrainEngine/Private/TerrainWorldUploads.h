#pragma once

#include "CoreMinimal.h"

#include "Bridge/tile_streamer.hpp"

class UTerrainMeshBackend;
struct FTerrainUploads;
struct FTerrainWorldState;

// The uploads of the tiles to the meshes (agent 38, TerrainWorldUploads.cpp), within the time of
// a frame (Bridge/upload_pacer.hpp): the real time read after each upload stops the frame,
// LoadBudget::uploadMilliseconds; one upload a frame at least. An update not finished goes on at
// the next frames, the streamer asked for a new one only then; its moves and removals once all
// its uploads are applied. Each upload timed, recorded in the timeline of the loads and in the
// cost of the uploads (UploadCostModel).

// The uploads of this frame: the next update of the streamer around Focus if the last one is
// applied whole, then its uploads within the budget.
FTerrainUploads TickTerrainUploads(UTerrainMeshBackend& Backend, FTerrainWorldState& State,
	const FVector& Focus);

// What is left of the update being applied, at once (before a warm-up applies updates of its own,
// the order of the updates kept).
void FlushTerrainUploads(UTerrainMeshBackend& Backend, FTerrainWorldState& State);

// A whole update at once (the warm-up of a new pawn).
void ApplyTerrainUpdateNow(UTerrainMeshBackend& Backend, FTerrainWorldState& State,
	const terrain::unreal::StreamerUpdate& Update);

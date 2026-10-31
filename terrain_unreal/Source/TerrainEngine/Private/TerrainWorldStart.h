#pragma once

#include "CoreMinimal.h"
#include "Templates/PimplPtr.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"

#include "Bridge/terrain_session.hpp"

#include <future>
#include <string>

class APawn;
class ATerrainWorld;
class SWidget;
struct FTerrainWorldState;

// The start of ATerrainWorld off the game thread (agent 38, TerrainWorldStart.cpp): the planetary
// map, the planet level, the hydrology, the tiles and their workers are built on a thread while
// the game goes on behind a black screen, the pawn held; "Chargement du monde…" shows on the
// black after LoadingSettings.MessageDelaySeconds. Then the rest of the start (the horizon, the
// water, the sky) runs on the game thread, and the load of the spawn begins: its fade, from then.
struct FTerrainWorldStart
{
	std::future<TPimplPtr<FTerrainWorldState>> Build;
	double StartSeconds = 0.0;
	double WorstFrameMilliseconds = 0.0; // on the game thread while the world builds
	bool bBlack = false;
	TWeakObjectPtr<APawn> HeldPawn;
	TSharedPtr<SWidget> Message;
	// For the log of the planet level once built.
	std::string PlanetFile;
	FPlatformMemoryStats Before;
};

// Launches the build of the session and of the frame placing (CoreOriginX, CoreOriginZ) at the
// Unreal origin, on a thread.
void BeginTerrainStart(FTerrainWorldStart& Start, const terrain::unreal::SessionParams& Params,
	double CoreOriginX, double CoreOriginZ);

// Once per frame while the world builds: the black screen, the held pawn, the message. Returns
// the state once built, the black screen and the held pawn handed over to its loads; an empty
// pointer while it builds. Rethrows what the build threw.
TPimplPtr<FTerrainWorldState> TickTerrainStart(ATerrainWorld& World, FTerrainWorldStart& Start,
	APawn* Pawn);

// The message removed and the pawn given back (EndPlay while the world builds).
void EndTerrainStart(ATerrainWorld& World, FTerrainWorldStart& Start);

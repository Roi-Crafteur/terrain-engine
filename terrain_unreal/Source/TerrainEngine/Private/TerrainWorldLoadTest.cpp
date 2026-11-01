// Terrain.LoadTest and ATerrainWorld::LoadTest (agent 38): TeleportWhenReady of the pawn of the
// player, the load measured (TerrainWorldLoading.cpp). Out of TerrainWorldLoadingPhase.cpp, which
// holds the one teleport (BeginTeleport), to keep both under 500 lines.
#include "TerrainWorld.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "TerrainEngineLog.h"
#include "TerrainWorldState.h"

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

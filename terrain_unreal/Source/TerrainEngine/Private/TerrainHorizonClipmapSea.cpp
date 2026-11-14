// The sea near a pixel in the materials of the horizon and of the tiles (agent 52: the land below
// the sea level is not the sea, Scripts/temporary_terrain_hlsl.py: SEA_NEAR_HLSL), and the console
// command Terrain.SeaNear 0|1 (with TERRAIN_DEV_TOOLS only) that turns it on (1, the default) or
// off (0: the look of before, the wet rock by the altitude alone), the scalar parameter SeaNear of
// both materials, for the comparisons A/B. Provisional: it moves to Terrain.Param (agent 40) once
// that is on main.
#include "TerrainHorizonClipmap.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TerrainEngineLog.h"
#include "UObject/UObjectIterator.h"

namespace
{
	const FName SeaNearParameter(TEXT("SeaNear"));
} // namespace

// The console command is a tool of the developers: never in a Shipping build (agent 36,
// tests/unreal_dev_tools_guard.cmake).
#if TERRAIN_DEV_TOOLS
namespace
{

	// Terrain.SeaNear 0|1: on every horizon of the world and the tiles bound to its level 0.
	void RunTerrainSeaNear(const TArray<FString>& Args, UWorld* World)
	{
		if (World == nullptr || Args.Num() < 1)
		{
			UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain.SeaNear 0|1"));
			return;
		}
		const bool bOn = FCString::Atoi(*Args[0]) != 0;
		for (TObjectIterator<UTerrainHorizonClipmap> It; It; ++It)
		{
			if (It->GetWorld() == World)
			{
				It->SetSeaNear(bOn);
			}
		}
		UE_LOG(LogTerrainEngine, Log, TEXT("Terrain.SeaNear %d: %s"), bOn ? 1 : 0,
			bOn ? TEXT("the sea near the pixel read (agent 52)")
				: TEXT("the look of before (the wet rock by the altitude)"));
	}

	FAutoConsoleCommandWithWorldAndArgs TerrainSeaNearCommand(TEXT("Terrain.SeaNear"),
		TEXT("Reads (1) or not (0, the look of before) the sea near the pixel in the materials of the "
			 "horizon and of the tiles: the wet rock below the sea level, the depth offset under the "
			 "tiles. Terrain.SeaNear 0|1"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunTerrainSeaNear));
} // namespace
#endif // TERRAIN_DEV_TOOLS

void UTerrainHorizonClipmap::SetSeaNear(bool bOn)
{
	for (TArray<TObjectPtr<UMaterialInstanceDynamic>>* Instances : {&Looks, &SurfaceLooks})
	{
		for (UMaterialInstanceDynamic* Look : *Instances)
		{
			Look->SetScalarParameterValue(SeaNearParameter, bOn ? 1.0f : 0.0f);
		}
	}
}

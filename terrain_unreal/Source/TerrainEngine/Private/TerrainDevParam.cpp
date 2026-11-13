// The console command Terrain.Param of the tools of the developers (agent 40): a scalar parameter
// of the look set on the materials of the far horizon and of the tiles (their ground and their
// water), to tell in game which part of a material makes what the screen shows (DebugView 7 for the
// normals, DetailNormals 0 for the normals of the grids, MacroVariation 0, SkyReflection...). The
// parameters the plugin drives itself (the placements of the levels, their reveal, the surface of
// level 0 under the tiles) are set back by it: only the parameters of the look stay
// (terrain_unreal/DEV_TOOLS.md). Never in a Shipping build.
#include "TerrainHorizonClipmap.h"

#if TERRAIN_DEV_TOOLS

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TerrainDevState.h"
#include "TerrainEngineLog.h"
#include "UObject/UObjectIterator.h"

#include <optional>

namespace
{
	// Terrain.Param <name> <value>: on every horizon of the world (the tiles bound to its level 0
	// with it; the materials made later keep their default).
	void RunTerrainParam(const TArray<FString>& Args, UWorld* World)
	{
		const std::optional<double> Number = Args.Num() == 2
			? terrain::unreal::parseNumber(TerrainDev::ToUtf8(Args[1]))
			: std::nullopt;
		if (World == nullptr || !Number)
		{
			UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain.Param <name> <value>"));
			return;
		}
		const FName Name(*Args[0]);
		const float Value = static_cast<float>(*Number);
		int32 Count = 0;
		for (TObjectIterator<UTerrainHorizonClipmap> It; It; ++It)
		{
			if (It->GetWorld() == World)
			{
				Count += It->SetLookScalar(Name, Value);
			}
		}
		UE_LOG(LogTerrainEngine, Log,
			TEXT("Terrain.Param %s %g: set on %d materials of the horizon and the tiles (those "
				 "without it ignore it)"),
			*Name.ToString(), Value, Count);
	}

	FAutoConsoleCommandWithWorldAndArgs TerrainParamCommand(TEXT("Terrain.Param"),
		TEXT("Sets a scalar parameter of the look on the materials of the far horizon and of the "
			 "tiles: Terrain.Param <name> <value> (DebugView 7, DetailNormals 0, ...)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunTerrainParam));
} // namespace

#endif // TERRAIN_DEV_TOOLS

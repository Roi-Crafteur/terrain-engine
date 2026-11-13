// The light floor of the edited surfaces (agent 41): the surfaces the edits of the players made
// emit a share of their base color (M_TemporaryTerrain, terrain_edit_light_nodes.py: the blue of
// the vertex colors of the edited tiles, Bridge/tile_mesh_conversion.hpp), against holes the
// lighting of the level leaves near black. Off by default (Building.bEditLightFloor): the console
// commands Terrain.EditLight 0|1 and Terrain.EditLightFloor <share> turn it on and set it, in
// every build, as Terrain.EditSave.
#include "TerrainWorldBuild.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TerrainEngineLog.h"
#include "TerrainWorld.h"

namespace
{
	const FName EditLightOnParameter(TEXT("EditLightOn"));
	const FName EditLightFloorParameter(TEXT("EditLightFloor"));

	// Applies the floor of every Terrain World of the world, and says what it is.
	void ApplyAll(UWorld* World)
	{
		for (TActorIterator<ATerrainWorld> It(World); It; ++It)
		{
			ATerrainWorld& Terrain = **It;
			if (!FTerrainBuildHooks::ApplyEditLight(Terrain))
			{
				UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain World: no light floor, the tiles "
					"have no instance of M_TemporaryTerrain (run "
					"create_temporary_terrain_material.py)"));
				continue;
			}
			UE_LOG(LogTerrainEngine, Log, TEXT("Terrain World: light floor of the edits %s (%.2f "
				"of the base color)"), Terrain.Building.bEditLightFloor ? TEXT("on") : TEXT("off"),
				Terrain.Building.EditLightFloor);
		}
	}

	// Terrain.EditLight [0|1]: without an argument, the state.
	void RunEditLight(const TArray<FString>& Args, UWorld* World)
	{
		if (World != nullptr && Args.Num() == 0)
		{
			for (TActorIterator<ATerrainWorld> It(World); It; ++It)
			{
				UE_LOG(LogTerrainEngine, Log, TEXT("Terrain World: light floor of the edits %s "
					"(%.2f of the base color)"), It->Building.bEditLightFloor ? TEXT("on") : TEXT("off"),
					It->Building.EditLightFloor);
			}
			return;
		}
		if (World == nullptr || Args.Num() != 1 || (Args[0] != TEXT("0") && Args[0] != TEXT("1")))
		{
			UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain.EditLight [0|1]"));
			return;
		}
		for (TActorIterator<ATerrainWorld> It(World); It; ++It)
		{
			It->Building.bEditLightFloor = Args[0] == TEXT("1");
		}
		ApplyAll(World);
	}

	// Terrain.EditLightFloor <share of the base color, 0 to 1>
	void RunEditLightFloor(const TArray<FString>& Args, UWorld* World)
	{
		if (World == nullptr || Args.Num() != 1 || !Args[0].IsNumeric())
		{
			UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain.EditLightFloor <share, 0 to 1>"));
			return;
		}
		const float Share = FMath::Clamp(FCString::Atof(*Args[0]), 0.0f, 1.0f);
		for (TActorIterator<ATerrainWorld> It(World); It; ++It)
		{
			It->Building.EditLightFloor = Share;
		}
		ApplyAll(World);
	}

	FAutoConsoleCommandWithWorldAndArgs EditLightCommand(TEXT("Terrain.EditLight"),
		TEXT("The light floor of the surfaces the edits made (1: on, 0: off, the default; "
			 "nothing: the state)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunEditLight));
	FAutoConsoleCommandWithWorldAndArgs EditLightFloorCommand(TEXT("Terrain.EditLightFloor"),
		TEXT("The share of their base color the surfaces the edits made emit with "
			 "Terrain.EditLight 1 (0 to 1, 0.15 by default)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunEditLightFloor));
} // namespace

bool FTerrainBuildHooks::ApplyEditLight(ATerrainWorld& World)
{
	UMaterialInstanceDynamic* Look = World.TileSurfaceLook.Get();
	if (Look == nullptr)
	{
		return false;
	}
	Look->SetScalarParameterValue(EditLightOnParameter,
		World.Building.bEditLightFloor ? 1.0f : 0.0f);
	Look->SetScalarParameterValue(EditLightFloorParameter,
		FMath::Clamp(World.Building.EditLightFloor, 0.0f, 1.0f));
	return true;
}

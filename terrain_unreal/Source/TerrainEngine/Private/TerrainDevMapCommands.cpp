// The console commands of the map (agent 35), on the map of every Terrain World of the world:
// Terrain.Map, Terrain.Map.Minimap 0|1, Terrain.Map.MinimapZoom <m/px>, Terrain.Map.MinimapRotate 0|1.
// The commands of views and places are those of the tools of agent 36 (Terrain.Goto, Terrain.Save).
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "TerrainDevMap.h"
#include "TerrainEngineLog.h"
#include "TerrainWorld.h"

namespace
{
	template <class Action>
	void ForEachMap(UWorld* World, Action&& Do)
	{
		if (World == nullptr)
		{
			return;
		}
		for (TActorIterator<ATerrainWorld> It(World); It; ++It)
		{
			if (UTerrainDevMapComponent* Map = It->DevMap.Get())
			{
				Do(*Map);
			}
		}
	}

	void RunMap(const TArray<FString>&, UWorld* World)
	{
		ForEachMap(World, [](UTerrainDevMapComponent& Map) { Map.ToggleDevMap(); });
	}

	void RunMinimap(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain.Map.Minimap 0|1"));
			return;
		}
		const bool bShown = FCString::Atoi(*Args[0]) != 0;
		ForEachMap(World, [bShown](UTerrainDevMapComponent& Map) { Map.SetMinimapShown(bShown); });
	}

	void RunMinimapZoom(const TArray<FString>& Args, UWorld* World)
	{
		const float MetersPerPixel = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 0.0f;
		if (!(MetersPerPixel > 0.0f))
		{
			UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain.Map.MinimapZoom <meters per pixel>"));
			return;
		}
		ForEachMap(World,
			[MetersPerPixel](UTerrainDevMapComponent& Map) { Map.SetMinimapZoom(MetersPerPixel); });
	}

	void RunMinimapRotate(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain.Map.MinimapRotate 0|1"));
			return;
		}
		const bool bRotates = FCString::Atoi(*Args[0]) != 0;
		ForEachMap(World, [bRotates](UTerrainDevMapComponent& Map) { Map.SetMinimapRotates(bRotates); });
	}

	FAutoConsoleCommandWithWorldAndArgs MapCommand(TEXT("Terrain.Map"),
		TEXT("Opens or closes the full-screen dev map (as its key, M by default)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunMap));

	FAutoConsoleCommandWithWorldAndArgs MinimapCommand(TEXT("Terrain.Map.Minimap"),
		TEXT("Shows (1) or hides (0) the minimap: Terrain.Map.Minimap 0|1"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunMinimap));

	FAutoConsoleCommandWithWorldAndArgs MinimapZoomCommand(TEXT("Terrain.Map.MinimapZoom"),
		TEXT("Meters of the world per pixel of the minimap, 0.25 to 4096: Terrain.Map.MinimapZoom 4"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunMinimapZoom));

	FAutoConsoleCommandWithWorldAndArgs MinimapRotateCommand(TEXT("Terrain.Map.MinimapRotate"),
		TEXT("The minimap turns with the player (1) or keeps the north up (0): "
			 "Terrain.Map.MinimapRotate 0|1"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunMinimapRotate));
} // namespace

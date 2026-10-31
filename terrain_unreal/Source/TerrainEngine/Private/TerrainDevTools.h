#pragma once

#include "CoreMinimal.h"
#include "Templates/PimplPtr.h"
#include "TerrainDevTypes.h"
#include "UObject/Object.h"

#include "Bridge/dev_commands.hpp"
#include "Bridge/place_pose.hpp"

#include "terrain/dev/places_file.hpp"

#include <string>
#include <vector>

#include "TerrainDevTools.generated.h"

class APawn;
class APlayerController;
class ATerrainWorld;
class UCanvas;
struct FTerrainDevState;

/**
 * The tools of the developers of an ATerrainWorld while playing (agent 36,
 * terrain_unreal/DEV_TOOLS.md): the screen of F3 (the player, the point aimed at, the
 * performance), the Terrain.* console commands (views, places, images, view modes, seed) and the
 * one teleport of the tools (TeleportView). What they say and decide comes from the pure C++ of
 * Bridge/ (place_pose, dev_commands, point_info, ground_ray, dev_perf, shot_waiter, debug_colors,
 * planet_job) and the places of terrain_core dev/; this class only shows and moves.
 *
 * The class exists in every build (Unreal reflects it), its code only with TERRAIN_DEV_TOOLS:
 * never in Shipping (TerrainEngine.Build.cs). Created by ATerrainWorld::BeginPlay when bDevTools,
 * stopped by its EndPlay.
 */
UCLASS(Transient)
class UTerrainDevTools : public UObject
{
	GENERATED_BODY()

public:
	// TerrainDevTools.cpp: the screen drawn over the game, F3 bound; Stop gives the pawn of the
	// game back, the view mode normal, and cancels a build of a planet.
	void Start(ATerrainWorld& InWorld);
	void Stop();
	// Each frame, before the streaming: the measures, the screen, the waits, the planet (the
	// planet alone while the world is not playing).
	void Tick(float DeltaSeconds);
	// The streaming stopped on an error: the waits end, the pawn moves again.
	void OnStreamingStopped();

	// The console commands (TerrainDevCommands.cpp), their arguments in UTF-8.
	void RunHud(const std::vector<std::string>& Args);
	void RunGoto(const std::vector<std::string>& Args);
	void RunLook(const std::vector<std::string>& Args);
	void RunEye(const std::vector<std::string>& Args);
	void RunPawn();
	void RunSave(const std::vector<std::string>& Args);
	void RunInfo(const std::vector<std::string>& Args);
	void RunShot(const std::vector<std::string>& Args);
	void RunViews(const std::vector<std::string>& Args);
	void RunView(const std::vector<std::string>& Args);
	void RunSeed(const std::vector<std::string>& Args);

	// TerrainDevView.cpp: ATerrainWorld::TeleportView, IsViewLoading and ReturnToGamePawn
	// (placed on the ground under the view, refused over water; or left where it stood).
	bool TeleportView(double X, double Z, double Heading, double Pitch, double Eye,
		ETerrainEyeMode EyeMode);
	bool IsViewLoading() const;
	bool ReturnToGamePawn(bool bUnderView);

private:
	// TerrainDevTools.cpp
	void TickWorld(float DeltaSeconds);
	APlayerController* Controller() const;
	void BindKeys();
	void NextPage();
	bool CurrentPose(terrain::unreal::ViewPose& OutPose) const;
	bool HasPlanet() const;
	// The places of the places file (Terrain.PlacesFile, by default data/dev/places.json of the
	// repository), or false with the reason in the log; Missing: an empty file of places.
	bool LoadPlacesFile(terrain::dev::PlacesFile& OutPlaces, bool bMissingIsEmpty) const;
	FString PlacesPath() const;

	// TerrainDevScreen.cpp
	void Draw(UCanvas* Canvas, APlayerController* PlayerController);
	void RefreshScreen(bool bNow);
	void ColumnLines(TArray<FString>& Lines) const;
	void AimedLines(TArray<FString>& Lines) const;
	void PerfLines(TArray<FString>& Lines) const;

	// TerrainDevView.cpp
	APawn* EnsureDevPawn(APlayerController& PlayerController);
	bool OverWater(const FVector& ViewLocation) const;
	bool PlaceView(const terrain::unreal::ViewPose& Pose);
	void TickHold();
	void EndHold();

	// TerrainDevShots.cpp
	void StartShot(const FString& File);
	void NextView();
	void TickShots();
	void Capture();
	void OnShotProcessed();
	void FinishViews();

	// TerrainDevLooks.cpp
	void SetViewMode(terrain::unreal::ViewMode Mode);
	void TickLooks();

	// TerrainDevSeed.cpp
	void TickSeed();
	void SwitchSeed(const std::string& PlanetFile);

	UPROPERTY()
	TObjectPtr<ATerrainWorld> World;

	// The flying pawn of the tools while the player has it, and the pawn of the game it left.
	UPROPERTY()
	TObjectPtr<APawn> DevPawn;

	UPROPERTY()
	TObjectPtr<APawn> GamePawn;

	TPimplPtr<FTerrainDevState> Dev;
	FDelegateHandle DrawHandle;
	FDelegateHandle ShotHandle;
	bool bKeysBound = false;
};

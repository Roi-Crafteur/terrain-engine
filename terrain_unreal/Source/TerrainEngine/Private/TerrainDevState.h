#pragma once

// The state of the tools of the developers (UTerrainDevTools) in plain C++: the page of F3, the
// view mode, the measures, the waits of the teleports and of the images, the build of a planet.
// Only with TERRAIN_DEV_TOOLS.
#if TERRAIN_DEV_TOOLS

#include "CoreMinimal.h"

#include "Bridge/dev_commands.hpp"
#include "Bridge/dev_perf.hpp"
#include "Bridge/place_pose.hpp"
#include "Bridge/planet_job.hpp"
#include "Bridge/shot_waiter.hpp"
#include "TerrainEngineLog.h"

#include "terrain/dev/places_file.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// The pawn of a teleport held still until the tiles around it are loaded (TeleportView).
struct FTerrainDevHold
{
	double Start = 0.0; // FPlatformTime::Seconds
	terrain::unreal::ShotWaiter Waiter;
};

// An image waited for (Terrain.Shot), or a run of them over places (Terrain.Views).
struct FTerrainDevShot
{
	enum class EPhase : uint8
	{
		Waiting,   // for the streaming to settle
		Capturing, // the screenshot asked, not yet written
	};
	std::vector<terrain::dev::Place> Places; // Terrain.Views, in their order; empty for a Shot
	std::size_t Index = 0;                   // the place being taken
	FString Folder;                          // Terrain.Views: the dated folder
	FString File;                            // the image being taken
	EPhase Phase = EPhase::Waiting;
	double Start = 0.0;                      // the pose, FPlatformTime::Seconds
	double CaptureStart = 0.0;
	std::optional<terrain::unreal::ShotWaiter> Waiter;
	terrain::unreal::ViewRecord Current;
	std::vector<terrain::unreal::ViewRecord> Records;
	// Where the run started, to go back there.
	terrain::unreal::ViewPose StartPose;
	bool bStartedInDevView = false;
};

struct FTerrainDevState
{
	terrain::unreal::HudPage Page = terrain::unreal::HudPage::Off;
	terrain::unreal::ViewMode Mode = terrain::unreal::ViewMode::Normal;
	// F3 also runs "viewmode lit" in the builds that are not Shipping: the mode is applied again.
	bool bReapplyMode = false;
	terrain::unreal::PerfWindow Perf{5.0};
	TArray<FString> Lines;     // the page shown, refreshed a few times a second
	double LinesAt = -1.0;     // FPlatformTime::Seconds of the last refresh
	double ProbeMs = 0.0;      // what the last refresh cost the game thread
	bool bHideScreen = false;  // during a capture
	std::optional<FTerrainDevHold> Hold;
	std::optional<FTerrainDevShot> Shot;
	// Terrain.Seed: the planet being prepared and the world it is for.
	std::unique_ptr<terrain::unreal::PlanetJob> Planet;
	terrain::unreal::SeedCommand PendingSeed;
};

namespace TerrainDev
{
	// The settings of the tools (console variables, TerrainDevTools.cpp).
	FString PlacesFileSetting();
	double ShotTimeoutSeconds();
	double ShotSettleSeconds();

	inline std::string ToUtf8(const FString& Text)
	{
		const FTCHARToUTF8 Converted(*Text);
		return std::string(Converted.Get(), static_cast<std::size_t>(Converted.Length()));
	}

	inline FString ToText(const std::string& Text)
	{
		return FString(UTF8_TO_TCHAR(Text.c_str()));
	}

	// A path of Unreal as a path of the standard library (UTF-8, whatever the code page).
	inline std::filesystem::path ToPath(const FString& Path)
	{
		const std::string Utf8 = ToUtf8(Path);
		return std::filesystem::path(reinterpret_cast<const char8_t*>(Utf8.c_str()));
	}

	inline void Log(const FString& Line)
	{
		UE_LOG(LogTerrainEngine, Log, TEXT("Terrain dev: %s"), *Line);
	}

	inline void Warn(const FString& Line)
	{
		UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain dev: %s"), *Line);
	}
} // namespace TerrainDev

#endif // TERRAIN_DEV_TOOLS

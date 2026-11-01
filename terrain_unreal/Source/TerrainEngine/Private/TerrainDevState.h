#pragma once

// The state of the tools of the developers (UTerrainDevTools) in plain C++: the page of F3, the
// view mode, the measures, the waits of the teleports and of the images, the build of a planet.
// Only with TERRAIN_DEV_TOOLS.
#if TERRAIN_DEV_TOOLS

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

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

class APawn;

// The pawn of the game just put down by Terrain.Pawn, watched (TerrainDevPawn.cpp): the spots
// that passed the rays, the one it stands on, since when.
struct FTerrainDevLanding
{
	TWeakObjectPtr<APawn> Pawn;
	TArray<FVector> Spots;
	int32 Index = 0;
	int32 Frames = 0;      // ticks since it was put there
	double PlacedAt = 0.0; // FPlatformTime::Seconds
};

// An image waited for (Terrain.Shot), or a run of them over places (Terrain.Views).
struct FTerrainDevShot
{
	enum class EPhase : uint8
	{
		Waiting,   // for the streaming to settle
		Capturing, // the screenshot asked (or the frames before the view is read), not written
	};
	std::vector<terrain::dev::Place> Places; // Terrain.Views, in their order; empty for a Shot
	std::size_t Index = 0;                   // the place being taken
	FString Folder;                          // Terrain.Views: the dated folder
	FString File;                            // the image being taken
	EPhase Phase = EPhase::Waiting;
	double Start = 0.0;                      // the pose, FPlatformTime::Seconds
	double CaptureStart = 0.0;
	// How the image is read (TerrainDevCapture.cpp), once CaptureFrames frames are drawn without
	// the screen of F3: Terrain.ShotShowUI 0, the render target of the viewport; 1, the viewport
	// with its UI (its widget, as Slate draws it); the whole window by Unreal when Slate cannot.
	enum class ERead : uint8
	{
		Texture,
		Viewport,
		Window,
	};
	ERead Read = ERead::Texture;
	int32 CaptureFrames = 0;
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
	// While an image or a run is taken: ShowFlag.MotionBlur 0, the look input ignored, and the
	// screen messages off during a read of the view (SetShotLook).
	bool bMotionBlurOff = false;
	bool bLookIgnored = false;
	bool bScreenMessagesOff = false;
	std::optional<FTerrainDevLanding> Landing;
	// The view mode of the tools for a world of another seed, set once it is built (Terrain.Seed).
	std::optional<terrain::unreal::ViewMode> ModeAfterStart;
	std::optional<FTerrainDevShot> Shot;
	// Terrain.Seed: the planet being prepared and the world it is for.
	std::unique_ptr<terrain::unreal::PlanetJob> Planet;
	terrain::unreal::SeedCommand PendingSeed;
	// PlanetFile as the level gives it (its "{Seed}"): Terrain.Seed sets the file it prepared.
	FString ConfiguredPlanetFile;
};

namespace TerrainDev
{
	// The settings of the tools (console variables, TerrainDevTools.cpp).
	FString PlacesFileSetting();
	double ShotTimeoutSeconds();
	double ShotSettleSeconds();
	double PawnRadiusMeters();
	bool ShotShowUI();
	// The size of a PNG from its header (TerrainDevCapture.cpp); false when it cannot be read.
	bool PngFileSize(const FString& File, int32& OutWidth, int32& OutHeight);

	inline std::string ToUtf8(const FString& Text)
	{
		const FTCHARToUTF8 Converted(*Text);
		// Get() is UTF8CHAR (not char) in Unreal 5: the same bytes.
		return std::string(reinterpret_cast<const char*>(Converted.Get()),
			static_cast<std::size_t>(Converted.Length()));
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

	// The rotation of a view (Bridge/place_pose: the pitch negative down, as FRotator).
	inline FRotator ToRotator(const terrain::unreal::UnrealRotation& Rotation)
	{
		return FRotator(Rotation.pitch, Rotation.yaw, Rotation.roll);
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

// The images of the tools of the developers (agent 36): Terrain.Shot takes the view once what the
// camera sees is loaded (every tile around shown, the horizon done, a second of settling; a delay
// at most, Terrain.ShotTimeout), Terrain.Views takes one image of each place of a list in a dated
// folder with its views.txt, the base of the comparisons before and after a change. The waits and
// the names come from Bridge/shot_waiter; the screen of F3 is hidden while an image is taken.
#include "TerrainDevTools.h"

#if TERRAIN_DEV_TOOLS

#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TerrainDevState.h"
#include "TerrainWorld.h"
#include "TerrainWorldState.h"
#include "UnrealClient.h"

#include <exception>
#include <string>

namespace
{
	using TerrainDev::ToText;
	using terrain::unreal::ShotWait;

	// An image asked and not written after this long is given up (it is logged).
	constexpr double CaptureTimeoutSeconds = 10.0;

	FString SavedFolder(const TCHAR* Sub)
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TerrainEngine"), Sub));
	}

	terrain::unreal::ShotWaiter NewWaiter()
	{
		return terrain::unreal::ShotWaiter(
			{0.5, TerrainDev::ShotSettleSeconds(), TerrainDev::ShotTimeoutSeconds()});
	}

	const char* PresetOf(const ATerrainWorld& World)
	{
		return !World.bCaves ? "surface" : World.bCaveRegions ? "regions" : "caves";
	}
} // namespace

void UTerrainDevTools::RunShot(const std::vector<std::string>& Args)
{
	std::string Error;
	const std::optional<std::string> Name = terrain::unreal::parseShot(Args, Error);
	if (!Name || Dev->Shot.has_value())
	{
		TerrainDev::Warn(Name ? FString(TEXT("an image is already being taken")) : ToText(Error));
		return;
	}
	Dev->Shot.emplace();
	Dev->Shot->Current.place.name = *Name;
	CurrentPose(Dev->Shot->Current.pose);
	StartShot(FPaths::Combine(SavedFolder(TEXT("Shots")), ToText(*Name + ".png")));
}

void UTerrainDevTools::RunViews(const std::vector<std::string>& Args)
{
	std::string Error;
	const std::optional<std::vector<std::string>> Patterns = terrain::unreal::parseViews(Args, Error);
	terrain::dev::PlacesFile Places;
	if (!Patterns || Dev->Shot.has_value() || !LoadPlacesFile(Places, false))
	{
		TerrainDev::Warn(!Patterns ? ToText(Error) : FString(TEXT("no views now")));
		return;
	}
	FTerrainDevShot Run;
	try
	{
		Run.Places = terrain::dev::matchPlaces(Places, *Patterns);
	}
	catch (const std::exception& Failure)
	{
		TerrainDev::Warn(ToText(Failure.what()));
		return;
	}
	const FDateTime Now = FDateTime::Now();
	Run.Folder = FPaths::Combine(SavedFolder(TEXT("Views")),
		ToText(terrain::unreal::viewsFolderName(Now.GetYear(), Now.GetMonth(), Now.GetDay(),
			Now.GetHour(), Now.GetMinute(), Now.GetSecond())));
	Run.bStartedInDevView = DevPawn != nullptr;
	CurrentPose(Run.StartPose);
	TerrainDev::Log(FString::Printf(TEXT("Terrain.Views: %d places into %s"),
		static_cast<int32>(Run.Places.size()), *Run.Folder));
	Dev->Shot = MoveTemp(Run);
	NextView();
}

void UTerrainDevTools::StartShot(const FString& File)
{
	FTerrainDevShot& Shot = *Dev->Shot;
	Shot.File = File;
	Shot.Current.file = TerrainDev::ToUtf8(FPaths::GetCleanFilename(File));
	Shot.Phase = FTerrainDevShot::EPhase::Waiting;
	Shot.Start = FPlatformTime::Seconds();
	Shot.Waiter = NewWaiter();
}

// The next place of a run of Terrain.Views: the view placed there, its image waited for.
void UTerrainDevTools::NextView()
{
	FTerrainDevShot& Shot = *Dev->Shot;
	if (Shot.Index >= Shot.Places.size())
	{
		FinishViews();
		return;
	}
	const terrain::dev::Place& Place = Shot.Places[Shot.Index];
	Shot.Current = {};
	Shot.Current.place = Place;
	Shot.Current.planetMissing = Place.planet && !HasPlanet();
	const terrain::HeightSample Sample =
		World->State->Session.tiles().heightfield().sampleAt(Place.x, Place.z);
	Shot.Current.pose = terrain::unreal::placePose(Place, terrain::unreal::columnTop(Sample));
	if (Shot.Current.planetMissing)
	{
		TerrainDev::Warn(FString::Printf(TEXT("%s needs the planet file: taken without it"),
			*ToText(Place.name)));
	}
	PlaceView(Shot.Current.pose);
	StartShot(FPaths::Combine(Shot.Folder,
		ToText(terrain::unreal::viewFileName(Shot.Index + 1, Place.name))));
}

void UTerrainDevTools::TickShots()
{
	if (!Dev->Shot.has_value())
	{
		return;
	}
	FTerrainDevShot& Shot = *Dev->Shot;
	const double Now = FPlatformTime::Seconds();
	if (Shot.Phase == FTerrainDevShot::EPhase::Capturing)
	{
		if (Now - Shot.CaptureStart > CaptureTimeoutSeconds)
		{
			TerrainDev::Warn(FString::Printf(TEXT("no image written after %.0f s: %s"),
				CaptureTimeoutSeconds, *Shot.File));
			OnShotProcessed();
		}
		return;
	}
	if (!Shot.Waiter.has_value())
	{
		NextView(); // the image before was written: the next place, or the end of the run
		return;
	}
	FTerrainWorldState& State = *World->State;
	terrain::unreal::StreamingState Streaming = terrain::unreal::tileState(State.Session.streamer());
	Streaming.horizonBusy = State.HorizonGpu != nullptr && State.HorizonGpu->busy();
	Streaming.horizonShown = State.HorizonGpu != nullptr ? State.HorizonGpu->shown()
		: State.Horizon != nullptr                       ? State.Horizon->shown()
														 : true;
	Streaming.editsPending = State.Session.editsPending();
	const ShotWait Wait = Shot.Waiter->update(Now - Shot.Start, Streaming);
	if (Wait == ShotWait::Waiting)
	{
		return;
	}
	Shot.Current.waitedSeconds = Now - Shot.Start;
	Shot.Current.ready = Wait == ShotWait::Ready;
	Shot.Current.state = Streaming;
	Shot.Current.framesPerSecond = Dev->Perf.summary().framesPerSecond;
	Capture();
}

// The screenshot of the next frame, the screen of F3 hidden, at the path given (no suffix).
void UTerrainDevTools::Capture()
{
	FTerrainDevShot& Shot = *Dev->Shot;
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Shot.File), true);
	Dev->bHideScreen = true;
	FScreenshotRequest::RequestScreenshot(Shot.File, false, false);
	Shot.Phase = FTerrainDevShot::EPhase::Capturing;
	Shot.CaptureStart = FPlatformTime::Seconds();
}

// Called by Unreal once a screenshot is written (any screenshot: only ours is taken here), and by
// TickShots when none comes.
void UTerrainDevTools::OnShotProcessed()
{
	if (!Dev.IsValid() || !Dev->Shot.has_value() ||
		Dev->Shot->Phase != FTerrainDevShot::EPhase::Capturing)
	{
		return;
	}
	Dev->bHideScreen = false;
	FTerrainDevShot& Shot = *Dev->Shot;
	TerrainDev::Log(FString::Printf(TEXT("image %s: %s after %.1f s"), *Shot.File,
		Shot.Current.ready ? TEXT("ready") : TEXT("NOT READY (timeout)"),
		Shot.Current.waitedSeconds));
	if (Shot.Places.empty())
	{
		Dev->Shot.reset();
		return;
	}
	Shot.Records.push_back(Shot.Current);
	++Shot.Index;
	// The next view on the next tick: never from within the drawing of the viewport.
	Shot.Phase = FTerrainDevShot::EPhase::Waiting;
	Shot.Waiter.reset();
}

void UTerrainDevTools::FinishViews()
{
	FTerrainDevShot& Shot = *Dev->Shot;
	const std::string Rings = std::to_string(World->Lod0DistanceChunks) + "/" +
		std::to_string(World->Lod1DistanceChunks) + "/" + std::to_string(World->Lod2DistanceChunks) +
		"/" + std::to_string(World->ViewDistanceChunks);
	std::string Text = terrain::unreal::viewsHeader(World->Seed, PresetOf(*World),
		HasPlanet() ? TerrainDev::ToUtf8(World->PlanetFile) : std::string(), Rings);
	for (const terrain::unreal::ViewRecord& Record : Shot.Records)
	{
		Text += terrain::unreal::describeView(Record) + "\n";
	}
	const FString Manifest = FPaths::Combine(Shot.Folder, TEXT("views.txt"));
	FFileHelper::SaveStringToFile(ToText(Text), *Manifest,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	TerrainDev::Log(FString::Printf(TEXT("Terrain.Views: %d images and views.txt in %s"),
		static_cast<int32>(Shot.Records.size()), *Shot.Folder));
	const terrain::unreal::ViewPose Back = Shot.StartPose;
	const bool bDevView = Shot.bStartedInDevView;
	Dev->Shot.reset();
	if (bDevView)
	{
		PlaceView(Back);
	}
	else
	{
		ReturnToGamePawn(false);
		if (APlayerController* PlayerController = Controller())
		{
			PlayerController->SetControlRotation(FRotator(Back.pitch, Back.yaw, 0.0));
		}
	}
}

#endif // TERRAIN_DEV_TOOLS

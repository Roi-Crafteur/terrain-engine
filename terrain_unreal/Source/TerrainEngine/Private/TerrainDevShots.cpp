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
	SetShotLook(true);
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
	SetShotLook(true);
	Dev->Shot = MoveTemp(Run);
	NextView();
}

// From the start of Terrain.Shot or Terrain.Views to its end: no motion blur in the images (the
// first one after a teleport came out blurred; ShowFlag.MotionBlur 2 gives the default of the
// viewport back), and the mouse does not turn the view (a possession resets the look input that
// is ignored: asked again on every tick of the run).
void UTerrainDevTools::SetShotLook(bool bTaking)
{
	APlayerController* PlayerController = Controller();
	if (Dev->bMotionBlurOff != bTaking && PlayerController != nullptr)
	{
		PlayerController->ConsoleCommand(
			bTaking ? TEXT("ShowFlag.MotionBlur 0") : TEXT("ShowFlag.MotionBlur 2"));
	}
	Dev->bMotionBlurOff = bTaking;
	if (PlayerController != nullptr)
	{
		if (bTaking && !PlayerController->IsLookInputIgnored())
		{
			PlayerController->SetIgnoreLookInput(true);
		}
		else if (!bTaking && Dev->bLookIgnored && PlayerController->IsLookInputIgnored())
		{
			PlayerController->SetIgnoreLookInput(false);
		}
	}
	Dev->bLookIgnored = bTaking;
	if (!bTaking)
	{
		SetScreenMessagesOff(false);
	}
}

// The messages of the screen are not in an image read from the view (Unreal leaves them out of
// its own screenshots without UI too).
void UTerrainDevTools::SetScreenMessagesOff(bool bOff)
{
	if (Dev->bScreenMessagesOff == bOff)
	{
		return;
	}
	if (APlayerController* PlayerController = Controller())
	{
		PlayerController->ConsoleCommand(
			bOff ? TEXT("DisableAllScreenMessages") : TEXT("EnableAllScreenMessages"));
	}
	Dev->bScreenMessagesOff = bOff;
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
	if (!World->State.IsValid())
	{
		TerrainDev::Warn(TEXT("Terrain.Views stopped: no world playing"));
		Dev->Shot.reset();
		return;
	}
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
	if (!PlaceView(Shot.Current.pose))
	{
		// The streaming stopped (OnStreamingStopped may have ended the run already).
		TerrainDev::Warn(TEXT("Terrain.Views stopped: the view could not be placed"));
		Dev->Shot.reset();
		return;
	}
	StartShot(FPaths::Combine(Shot.Folder,
		ToText(terrain::unreal::viewFileName(Shot.Index + 1, Place.name))));
}

void UTerrainDevTools::TickShots()
{
	if (!Dev->Shot.has_value())
	{
		if (Dev->bMotionBlurOff || Dev->bLookIgnored)
		{
			SetShotLook(false); // whichever way the image or the run ended
		}
		return;
	}
	SetShotLook(true);
	FTerrainDevShot& Shot = *Dev->Shot;
	const double Now = FPlatformTime::Seconds();
	if (Shot.Phase == FTerrainDevShot::EPhase::Capturing && Shot.bFromTexture)
	{
		if (--Shot.CaptureFrames > 0)
		{
			return;
		}
		Shot.Current.capture = MeasureCapture("texture");
		ReadViewTexture(Shot.File, Shot.Current.capture);
		SetScreenMessagesOff(false);
		OnShotProcessed();
		return;
	}
	if (Shot.Phase == FTerrainDevShot::EPhase::Capturing)
	{
		if (Now - Shot.CaptureStart > CaptureTimeoutSeconds)
		{
			TerrainDev::Warn(FString::Printf(TEXT("no image written after %.0f s: %s"),
				CaptureTimeoutSeconds, *Shot.File));
			FScreenshotRequest::Reset(); // never written later under the name of another view
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

// The image of the view at the path given (no suffix), the screen of F3 hidden, the pose set
// once more (nothing turns the view of an image): the render target of the viewport read two
// frames later (TerrainDevCapture.cpp), or with Terrain.ShotShowUI 1 the window, as shot showui.
void UTerrainDevTools::Capture()
{
	FTerrainDevShot& Shot = *Dev->Shot;
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Shot.File), true);
	Dev->bHideScreen = true;
	if (APlayerController* PlayerController = Controller())
	{
		PlayerController->SetControlRotation(
			TerrainDev::ToRotator(terrain::unreal::viewRotation(Shot.Current.pose)));
	}
	Shot.bFromTexture = !TerrainDev::ShotShowUI();
	if (Shot.bFromTexture)
	{
		SetScreenMessagesOff(true);
		Shot.CaptureFrames = 2;
	}
	else
	{
		Shot.Current.capture = MeasureCapture("window");
		FScreenshotRequest::RequestScreenshot(Shot.File, true, false);
	}
	Shot.Phase = FTerrainDevShot::EPhase::Capturing;
	Shot.CaptureStart = FPlatformTime::Seconds();
}

// Called by Unreal once a screenshot is written (any screenshot: only ours is taken here), and by
// TickShots when none comes.
void UTerrainDevTools::OnShotProcessed()
{
	if (!Dev.IsValid() || !Dev->Shot.has_value() ||
		Dev->Shot->Phase != FTerrainDevShot::EPhase::Capturing ||
		(Dev->Shot->bFromTexture && Dev->Shot->CaptureFrames > 0)) // another screenshot
	{
		return;
	}
	Dev->bHideScreen = false;
	FTerrainDevShot& Shot = *Dev->Shot;
	TerrainDev::Log(FString::Printf(TEXT("image %s: %s after %.1f s"), *Shot.File,
		Shot.Current.ready ? TEXT("ready") : TEXT("NOT READY (timeout)"),
		Shot.Current.waitedSeconds));
	const terrain::unreal::CaptureCheck& Check = Shot.Current.capture;
	if (Check.measured)
	{
		TerrainDev::Log(FString::Printf(TEXT("camera yaw %.2f pitch %.2f fov %.1f (pose yaw %.2f "
			"pitch %.2f); image %dx%d, viewport %dx%d, its texture %dx%d, read from the %s"),
			Check.camera.yaw, Check.camera.pitch, Check.fov, Shot.Current.pose.yaw,
			Shot.Current.pose.pitch, Check.imageWidth, Check.imageHeight, Check.viewportWidth,
			Check.viewportHeight, Check.textureWidth, Check.textureHeight, *ToText(Check.method)));
	}
	for (const std::string& Warning : terrain::unreal::captureWarnings(Shot.Current))
	{
		TerrainDev::Warn(FString::Printf(TEXT("%s: %s"), *Shot.File, *ToText(Warning)));
	}
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
	for (terrain::unreal::ViewRecord& Record : Shot.Records)
	{
		// The window taken by Unreal (Terrain.ShotShowUI 1): its size read back from the file.
		terrain::unreal::CaptureCheck& Check = Record.capture;
		if (Check.measured && Check.imageWidth == 0 &&
			!TerrainDev::PngFileSize(FPaths::Combine(Shot.Folder, ToText(Record.file)),
				Check.imageWidth, Check.imageHeight))
		{
			Check.imageWidth = 0;
			Check.imageHeight = 0;
		}
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
			PlayerController->SetControlRotation(
				TerrainDev::ToRotator(terrain::unreal::viewRotation(Back)));
		}
	}
}

#endif // TERRAIN_DEV_TOOLS

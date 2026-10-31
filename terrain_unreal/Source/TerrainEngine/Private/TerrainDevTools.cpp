// The tools of the developers (agent 36): their life with the Terrain World, the key F3, their
// settings, the pose of the camera and the file of the places (TerrainDevTools.h).
#include "TerrainDevTools.h"

#if TERRAIN_DEV_TOOLS

#include "Camera/PlayerCameraManager.h"
#include "Components/InputComponent.h"
#include "Debug/DebugDrawService.h"
#include "DynamicRHI.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "RenderTimer.h"
#include "TerrainDevState.h"
#include "TerrainWorld.h"
#include "TerrainWorldState.h"
#include "UnrealClient.h"

#include <exception>

namespace
{
	using terrain::unreal::HudPage;

	TAutoConsoleVariable<FString> CVarPlacesFile(TEXT("Terrain.PlacesFile"), TEXT(""),
		TEXT("The file of the places of the tools of the developers (Terrain.Goto, Save, Views); "
			 "empty: data/dev/places.json of the repository of the plugin"));
	TAutoConsoleVariable<float> CVarShotTimeout(TEXT("Terrain.ShotTimeout"), 60.0f,
		TEXT("Seconds a teleport or an image of the tools waits at most for the tiles and the "
			 "horizon around the view to load"));
	TAutoConsoleVariable<float> CVarShotSettle(TEXT("Terrain.ShotSettle"), 1.0f,
		TEXT("Seconds the streaming stays settled before an image is taken (the exposure and the "
			 "lighting follow)"));
} // namespace

FString TerrainDev::PlacesFileSetting()
{
	return CVarPlacesFile.GetValueOnGameThread();
}

double TerrainDev::ShotTimeoutSeconds()
{
	return FMath::Max(1.0, static_cast<double>(CVarShotTimeout.GetValueOnGameThread()));
}

double TerrainDev::ShotSettleSeconds()
{
	return FMath::Max(0.0, static_cast<double>(CVarShotSettle.GetValueOnGameThread()));
}

void UTerrainDevTools::Start(ATerrainWorld& InWorld)
{
	World = &InWorld;
	Dev = MakePimpl<FTerrainDevState>();
	Dev->ConfiguredPlanetFile = InWorld.PlanetFile;
	DrawHandle = UDebugDrawService::Register(TEXT("Game"),
		FDebugDrawDelegate::CreateUObject(this, &UTerrainDevTools::Draw));
	ShotHandle = FScreenshotRequest::OnScreenshotRequestProcessed().AddUObject(this,
		&UTerrainDevTools::OnShotProcessed);
	BindKeys();
	TerrainDev::Log(FString::Printf(TEXT("tools on: F3 for the screen, Terrain.* commands "
		"(terrain_unreal/DEV_TOOLS.md), places in %s"), *PlacesPath()));
}

void UTerrainDevTools::Stop()
{
	if (Dev.IsValid())
	{
		if (Dev->Mode != terrain::unreal::ViewMode::Normal)
		{
			SetViewMode(terrain::unreal::ViewMode::Normal);
		}
		EndHold();
		Dev->Shot.reset();
		Dev->Planet.reset(); // cancelled: waits at most for the end of the stage in progress
	}
	if (IsValid(DevPawn))
	{
		// The pawn of the game back, unless the whole world ends.
		const UWorld* GameWorld = World != nullptr ? World->GetWorld() : nullptr;
		APlayerController* PlayerController = Controller();
		if (GameWorld != nullptr && !GameWorld->bIsTearingDown && PlayerController != nullptr &&
			IsValid(GamePawn))
		{
			PlayerController->Possess(GamePawn);
		}
		DevPawn->Destroy();
	}
	DevPawn = nullptr;
	GamePawn = nullptr;
	UDebugDrawService::Unregister(DrawHandle);
	FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ShotHandle);
	if (bKeysBound && World != nullptr)
	{
		if (APlayerController* PlayerController = Controller())
		{
			World->DisableInput(PlayerController);
		}
	}
	bKeysBound = false;
	Dev.Reset();
}

void UTerrainDevTools::Tick(float DeltaSeconds)
{
	if (!Dev.IsValid() || World == nullptr)
	{
		return;
	}
	try
	{
		TickWorld(DeltaSeconds);
	}
	catch (const std::exception& Failure)
	{
		TerrainDev::Warn(FString::Printf(TEXT("the tools stopped what they did: %s"),
			*TerrainDev::ToText(Failure.what())));
		OnStreamingStopped();
	}
}

void UTerrainDevTools::OnStreamingStopped()
{
	if (!Dev.IsValid())
	{
		return;
	}
	EndHold();
	Dev->Shot.reset();
	Dev->bHideScreen = false;
}

void UTerrainDevTools::TickWorld(float DeltaSeconds)
{
	if (!World->State.IsValid())
	{
		TickSeed(); // a world of another seed may be on its way
		return;
	}
	BindKeys();
	const double Now = FPlatformTime::Seconds();
	Dev->Perf.addFrame(Now, {static_cast<double>(DeltaSeconds) * 1000.0,
		FPlatformTime::ToMilliseconds(GGameThreadTime),
		FPlatformTime::ToMilliseconds(GRenderThreadTime),
		FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0))});
	Dev->Perf.addTiles(Now, World->State->Session.timings().totals());
	TickHold();
	TickShots();
	TickLooks();
	TickSeed();
	RefreshScreen(false);
}

APlayerController* UTerrainDevTools::Controller() const
{
	return World != nullptr && World->GetWorld() != nullptr
		? World->GetWorld()->GetFirstPlayerController()
		: nullptr;
}

// F3 on the input of the Terrain World, pushed on the stack of the player controller: it stays
// whatever pawn the player has.
void UTerrainDevTools::BindKeys()
{
	APlayerController* PlayerController = Controller();
	if (bKeysBound || PlayerController == nullptr)
	{
		return;
	}
	World->EnableInput(PlayerController);
	if (World->InputComponent == nullptr)
	{
		return;
	}
	World->InputComponent->BindKey(EKeys::F3, IE_Pressed, this, &UTerrainDevTools::NextPage);
	bKeysBound = true;
}

void UTerrainDevTools::NextPage()
{
	Dev->Page = terrain::unreal::nextHudPage(Dev->Page);
	Dev->bReapplyMode = true;
	RefreshScreen(true);
}

void UTerrainDevTools::RunHud(const std::vector<std::string>& Args)
{
	std::string Error;
	const std::optional<HudPage> Page = terrain::unreal::parseHud(Args, Dev->Page, Error);
	if (!Page)
	{
		TerrainDev::Warn(TerrainDev::ToText(Error));
		return;
	}
	Dev->Page = *Page;
	RefreshScreen(true);
}

// The view of the camera of the player: its eye in the core, its yaw and pitch.
bool UTerrainDevTools::CurrentPose(terrain::unreal::ViewPose& OutPose) const
{
	const APlayerController* PlayerController = Controller();
	if (PlayerController == nullptr || PlayerController->PlayerCameraManager == nullptr ||
		!World->State.IsValid())
	{
		return false;
	}
	const FVector Camera = PlayerController->PlayerCameraManager->GetCameraLocation();
	const FRotator Rotation = PlayerController->PlayerCameraManager->GetCameraRotation();
	OutPose.eye = terrain::unreal::toCore(World->State->Frame,
		TerrainWorldConversions::ToUnrealPoint(Camera));
	OutPose.yaw = FRotator::NormalizeAxis(Rotation.Yaw);
	OutPose.pitch = FRotator::NormalizeAxis(Rotation.Pitch);
	return true;
}

bool UTerrainDevTools::HasPlanet() const
{
	return World->State.IsValid() && World->State->Session.planet().level != nullptr;
}

// Terrain.PlacesFile, or data/dev/places.json at the root of the repository the plugin lies in
// (terrain_unreal/..).
FString UTerrainDevTools::PlacesPath() const
{
	FString Path = TerrainDev::PlacesFileSetting();
	if (Path.IsEmpty())
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("TerrainEngine"));
		const FString Root = Plugin.IsValid()
			? FPaths::Combine(Plugin->GetBaseDir(), TEXT(".."))
			: FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("terrain-engine"));
		Path = FPaths::Combine(Root, UTF8_TO_TCHAR(terrain::dev::kPlacesFilePath));
	}
	Path = FPaths::ConvertRelativePathToFull(Path);
	FPaths::CollapseRelativeDirectories(Path);
	return Path;
}

bool UTerrainDevTools::LoadPlacesFile(terrain::dev::PlacesFile& OutPlaces,
	bool bMissingIsEmpty) const
{
	const FString Path = PlacesPath();
	if (bMissingIsEmpty && !FPaths::FileExists(Path))
	{
		OutPlaces = {};
		return true;
	}
	try
	{
		OutPlaces = terrain::dev::loadPlaces(TerrainDev::ToPath(Path));
		return true;
	}
	catch (const std::exception& Error)
	{
		TerrainDev::Warn(TerrainDev::ToText(Error.what()));
		return false;
	}
}

#endif // TERRAIN_DEV_TOOLS

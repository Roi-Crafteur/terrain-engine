// How the images of the tools of the developers are read (agent 36, Terrain.Shot and
// Terrain.Views): the render target of the viewport, read whole in its physical pixels, and what
// the camera really was then (Bridge/shot_waiter: CaptureCheck, in views.txt).
//
// The screenshot of Unreal without its UI (FScreenshotRequest) reads a rectangle of the size of
// the viewport; with a display scaled by Windows (200 %), the viewport of the editor counts its
// pixels at the scale of the desktop while its render target holds the physical ones, and the
// image held only the top left of the view: the horizon of a level view at its bottom edge.
// Terrain.ShotShowUI 1 takes the viewport with its UI instead, as Slate draws its widget (shot
// showui without the panels of the editor, the screen of F3 hidden); the whole window, by Unreal,
// only when Slate cannot.
#include "TerrainDevTools.h"

#if TERRAIN_DEV_TOOLS

#include "Camera/PlayerCameraManager.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "TerrainDevState.h"
#include "TerrainWorld.h"
#include "TerrainWorldState.h"
#include "UnrealClient.h"
#include "Widgets/SViewport.h"

#include <cstdint>

namespace
{
	FViewport* GameViewportOf(const ATerrainWorld& World)
	{
		const UWorld* GameWorld = World.GetWorld();
		UGameViewportClient* Client = GameWorld != nullptr ? GameWorld->GetGameViewport() : nullptr;
		return Client != nullptr ? Client->Viewport : nullptr;
	}
} // namespace

// The camera and the sizes of the view when its image is read.
terrain::unreal::CaptureCheck UTerrainDevTools::MeasureCapture(const char* Method) const
{
	terrain::unreal::CaptureCheck Check;
	Check.method = Method;
	const APlayerController* PlayerController = Controller();
	if (PlayerController != nullptr && PlayerController->PlayerCameraManager != nullptr)
	{
		const APlayerCameraManager& Camera = *PlayerController->PlayerCameraManager;
		CurrentPose(Check.camera);
		Check.fov = Camera.GetFOVAngle();
		Check.measured = true;
	}
	if (const FViewport* Viewport = GameViewportOf(*World))
	{
		const FIntPoint Size = Viewport->GetSizeXY();
		const FIntPoint Texture = Viewport->GetRenderTargetTextureSizeXY();
		Check.viewportWidth = Size.X;
		Check.viewportHeight = Size.Y;
		Check.textureWidth = Texture.X;
		Check.textureHeight = Texture.Y;
	}
	return Check;
}

// The render target of the viewport read whole, in its physical pixels, into a PNG at File (the
// last frame drawn: the screen of F3 hidden two frames before). False, logged, when it cannot be.
bool UTerrainDevTools::ReadViewTexture(const FString& File, terrain::unreal::CaptureCheck& Check)
{
	FViewport* Viewport = GameViewportOf(*World);
	const FIntPoint Size = Viewport != nullptr ? Viewport->GetRenderTargetTextureSizeXY()
											   : FIntPoint::ZeroValue;
	TArray<FColor> Pixels;
	if (Size.X <= 0 || Size.Y <= 0 ||
		!Viewport->ReadPixels(Pixels, FReadSurfaceDataFlags(), FIntRect(0, 0, Size.X, Size.Y)) ||
		Pixels.Num() != Size.X * Size.Y)
	{
		TerrainDev::Warn(FString::Printf(TEXT("the view could not be read (%dx%d): %s"), Size.X,
			Size.Y, *File));
		return false;
	}
	for (FColor& Pixel : Pixels)
	{
		Pixel.A = 255; // the scene leaves its own alpha there
	}
	if (!FImageUtils::SaveImageByExtension(*File, FImageView(Pixels.GetData(), Size.X, Size.Y)))
	{
		TerrainDev::Warn(FString::Printf(TEXT("the image could not be written: %s"), *File));
		return false;
	}
	Check.imageWidth = Size.X;
	Check.imageHeight = Size.Y;
	return true;
}

// The viewport of the game with its UI, as Slate draws its widget (the debug canvas, the HUD and
// the widgets over it; not the panels of the editor around it), into a PNG at File, the size of
// the widget in physical pixels. False, logged, when Slate cannot draw it alone.
bool UTerrainDevTools::ReadViewWidget(const FString& File, terrain::unreal::CaptureCheck& Check)
{
	const UWorld* GameWorld = World->GetWorld();
	UGameViewportClient* Client = GameWorld != nullptr ? GameWorld->GetGameViewport() : nullptr;
	const TSharedPtr<SViewport> Widget =
		Client != nullptr ? Client->GetGameViewportWidget() : TSharedPtr<SViewport>();
	TArray<FColor> Pixels;
	FIntVector Size(0, 0, 0);
	if (!Widget.IsValid() || !FSlateApplication::IsInitialized() ||
		!FSlateApplication::Get().TakeScreenshot(Widget.ToSharedRef(), Pixels, Size) ||
		Size.X <= 0 || Size.Y <= 0 || Pixels.Num() < Size.X * Size.Y)
	{
		TerrainDev::Warn(FString::Printf(TEXT("the viewport could not be drawn alone: the whole "
			"window is taken instead: %s"), *File));
		return false;
	}
	for (FColor& Pixel : Pixels)
	{
		Pixel.A = 255;
	}
	if (!FImageUtils::SaveImageByExtension(*File, FImageView(Pixels.GetData(), Size.X, Size.Y)))
	{
		TerrainDev::Warn(FString::Printf(TEXT("the image could not be written: %s"), *File));
		return false;
	}
	Check.imageWidth = Size.X;
	Check.imageHeight = Size.Y;
	return true;
}

// The size of a PNG written by Unreal (Terrain.ShotShowUI 1), from its header; false when it
// cannot be read (yet).
bool TerrainDev::PngFileSize(const FString& File, int32& OutWidth, int32& OutHeight)
{
	const TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*File));
	std::uint8_t Header[24] = {};
	if (!Reader.IsValid() || Reader->TotalSize() < 24)
	{
		return false;
	}
	Reader->Serialize(Header, sizeof(Header));
	const auto Size = terrain::unreal::pngSize(Header);
	if (!Size)
	{
		return false;
	}
	OutWidth = static_cast<int32>(Size->first);
	OutHeight = static_cast<int32>(Size->second);
	return true;
}

#endif // TERRAIN_DEV_TOOLS

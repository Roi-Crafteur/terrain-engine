// The full-screen dev map opened and closed, its keys, its view moved by the mouse, its layers, and
// the settings of the minimap (UTerrainDevMapComponent, TerrainDevMap.h).
#include "TerrainDevMap.h"

#include "Bridge/map_targets.hpp"
#include "Components/InputComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "STerrainDevMap.h"
#include "TerrainDevMapState.h"
#include "TerrainEngineLog.h"
#include "terrain/map/map_style.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
	// Above the minimap and the screens of the tools.
	constexpr int32 DevMapZOrder = 50;
	// The first spacing of the dev map, meters per pixel: about 120 km across a full HD screen.
	constexpr double FirstMetersPerPixel = 64.0;
	// A click within this many pixels of a saved place takes its view.
	constexpr double PlacePickPixels = 10.0;
} // namespace

bool UTerrainDevMapComponent::ToggleDevMap()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	if (!State.IsValid())
	{
		return false;
	}
	if (IsDevMapOpen())
	{
		CloseDevMap();
	}
	else
	{
		OpenDevMap();
	}
	return true;
#endif
}

bool UTerrainDevMapComponent::IsDevMapOpen() const
{
	return State.IsValid() && State->DevMap.IsValid();
}

void UTerrainDevMapComponent::SetMinimapShown(bool bShown)
{
	bMinimap = bShown;
}

void UTerrainDevMapComponent::SetMinimapZoom(float MetersPerPixel)
{
	MinimapMetersPerPixel = FMath::Clamp(MetersPerPixel, 0.25f, 4096.0f);
}

void UTerrainDevMapComponent::SetMinimapRotates(bool bRotates)
{
	bMinimapRotates = bRotates;
}

void UTerrainDevMapComponent::ZoomMinimapIn()
{
	SetMinimapZoom(MinimapMetersPerPixel * 0.5f);
}

void UTerrainDevMapComponent::ZoomMinimapOut()
{
	SetMinimapZoom(MinimapMetersPerPixel * 2.0f);
}

void UTerrainDevMapComponent::PressDevMapKey()
{
	ToggleDevMap();
}

// The keys of the map, on an input component of its own pushed on the first player controller
// once there is one.
void UTerrainDevMapComponent::BindKeys()
{
	APlayerController* Controller = GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (MapInput != nullptr || Controller == nullptr)
	{
		return;
	}
	// A name of its own each time: the keys may be bound again in the same play.
	UInputComponent* Input = NewObject<UInputComponent>(this);
	Input->bBlockInput = false;
	Input->BindKey(MinimapZoomInKey, IE_Pressed, this, &UTerrainDevMapComponent::ZoomMinimapIn);
	Input->BindKey(MinimapZoomOutKey, IE_Pressed, this, &UTerrainDevMapComponent::ZoomMinimapOut);
#if !UE_BUILD_SHIPPING
	Input->BindKey(DevMapKey, IE_Pressed, this, &UTerrainDevMapComponent::PressDevMapKey);
#endif
	Controller->PushInputComponent(Input);
	MapInput = Input;
}

void UTerrainDevMapComponent::UnbindKeys()
{
	APlayerController* Controller = GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (MapInput != nullptr && Controller != nullptr)
	{
		Controller->PopInputComponent(MapInput.Get());
	}
	MapInput = nullptr;
}

// Full screen over the game, the cursor shown, the game and the interface both listening but the
// character and the camera held still; the keys and the mouse stay in the map (STerrainDevMap).
void UTerrainDevMapComponent::OpenDevMap()
{
	UGameViewportClient* Viewport = GetWorld() != nullptr ? GetWorld()->GetGameViewport() : nullptr;
	if (!State.IsValid() || Viewport == nullptr || IsDevMapOpen())
	{
		return;
	}
	if (!State->bViewCentered)
	{
		CenterDevMapOnPlayer();
		State->View.metersPerPixel = FirstMetersPerPixel;
		State->bViewCentered = true;
	}
	State->DevMap = SNew(STerrainDevMap)
		.Map(TWeakObjectPtr<UTerrainDevMapComponent>(this))
		.CloseKey(DevMapKey);
	Viewport->AddViewportWidgetContent(State->DevMap.ToSharedRef(), DevMapZOrder);
	if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
	{
		FInputModeGameAndUI Mode;
		Mode.SetWidgetToFocus(State->DevMap);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		Controller->SetInputMode(Mode);
		Controller->SetShowMouseCursor(true);
		Controller->SetIgnoreMoveInput(true);
		Controller->SetIgnoreLookInput(true);
	}
	FSlateApplication::Get().SetKeyboardFocus(State->DevMap);
}

void UTerrainDevMapComponent::CloseDevMap()
{
	if (!IsDevMapOpen())
	{
		return;
	}
	if (UGameViewportClient* Viewport = GetWorld() != nullptr ? GetWorld()->GetGameViewport() : nullptr)
	{
		Viewport->RemoveViewportWidgetContent(State->DevMap.ToSharedRef());
	}
	State->DevMap.Reset();
	State->Hover.reset();
	if (APlayerController* Controller = GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		Controller->SetInputMode(FInputModeGameOnly());
		Controller->SetShowMouseCursor(false);
		Controller->SetIgnoreMoveInput(false);
		Controller->SetIgnoreLookInput(false);
	}
}

void UTerrainDevMapComponent::CenterDevMapOnPlayer()
{
	if (State.IsValid() && State->bHasPlayer)
	{
		State->View.center = State->Player;
	}
}

void UTerrainDevMapComponent::ZoomDevMap(const FVector2D& Pixel, float Factor)
{
	if (State.IsValid())
	{
		State->View = terrain::unreal::zoomAt(State->View, {Pixel.X, Pixel.Y}, Factor, State->World);
	}
}

void UTerrainDevMapComponent::PanDevMap(const FVector2D& Delta)
{
	if (State.IsValid())
	{
		State->View = terrain::unreal::panBy(State->View, Delta.X, Delta.Y, State->World);
	}
}

void UTerrainDevMapComponent::HoverDevMap(const FVector2D& Pixel)
{
	if (State.IsValid())
	{
		State->Hover = terrain::unreal::screenToWorld(State->View, {Pixel.X, Pixel.Y}, State->World);
	}
}

void UTerrainDevMapComponent::ResizeDevMap(const FVector2D& Size)
{
	if (State.IsValid())
	{
		State->View.width = Size.X;
		State->View.height = Size.Y;
	}
}

// A click on a saved place takes its view; elsewhere the point clicked, the heading kept.
void UTerrainDevMapComponent::ClickDevMap(const FVector2D& Pixel)
{
	if (!State.IsValid())
	{
		return;
	}
	const std::vector<terrain::unreal::MapPlace> Places = State->Places->places();
	for (int32 Index = 0; Index < static_cast<int32>(Places.size()); ++Index)
	{
		const terrain::unreal::ScreenPoint At = terrain::unreal::worldToScreen(
			State->View, {Places[Index].x, Places[Index].z}, State->World);
		if (FVector2D::Distance(FVector2D(At.x, At.y), Pixel) <= PlacePickPixels)
		{
			GoToPlace(Index);
			return;
		}
	}
	const terrain::unreal::MapPlace Target = terrain::unreal::clickTarget(
		State->View, {Pixel.X, Pixel.Y}, State->Heading, State->Pitch, State->World);
	RequestTeleport(Target.x, Target.z, Target.heading, Target.pitch, Target.eye, true,
		TEXT("the point clicked"));
}

TArray<FString> UTerrainDevMapComponent::LayerNames() const
{
	TArray<FString> Names;
	for (const std::string& Name : terrain::defaultMapLayers().names())
	{
		Names.Add(FString(UTF8_TO_TCHAR(Name.c_str())));
	}
	return Names;
}

bool UTerrainDevMapComponent::IsLayerShown(const FString& Layer) const
{
	if (!State.IsValid())
	{
		return false;
	}
	const std::vector<std::string> Shown = State->Cache->layers();
	return std::find(Shown.begin(), Shown.end(), std::string(TCHAR_TO_UTF8(*Layer))) != Shown.end();
}

// The layers drawn change: the tiles are rendered again (the old ones show until then).
void UTerrainDevMapComponent::SetLayerShown(const FString& Layer, bool bShown)
{
	if (!State.IsValid())
	{
		return;
	}
	const std::string Name(TCHAR_TO_UTF8(*Layer));
	const std::vector<std::string> Now = State->Cache->layers();
	std::vector<std::string> Shown;
	for (const std::string& Each : terrain::defaultMapLayers().names())
	{
		const bool bWasShown = std::find(Now.begin(), Now.end(), Each) != Now.end();
		if (Each == Name ? bShown : bWasShown)
		{
			Shown.push_back(Each);
		}
	}
	State->Cache->setLayers(Shown);
}

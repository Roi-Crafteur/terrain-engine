// The start of ATerrainWorld off the game thread (agent 38): the screen black from the first frame
// of Play; the session (the planetary map, the planet level, the hydrology, the tiles and their
// workers) built on a thread behind it, the pawn held, "Chargement du monde…" on the black after
// a delay; then the rest of the start on the game thread (ATerrainWorld::TickStart, FinishStreamingStart). The world is the
// same: the session is built as before, only on another thread.
#include "TerrainWorldStart.h"

#include "TerrainWorld.h"

#include "Brushes/SlateColorBrush.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "RenderTimer.h"
#include "Styling/CoreStyle.h"
#include "TerrainEngineLog.h"
#include "TerrainWorldState.h"
#include "TerrainWorldWater.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#include <chrono>
#include <exception>

namespace
{
	using TerrainWorldConversions::ToFString;

	// Above every other widget of the viewport: the black, the message over it.
	constexpr int32 StartCoverZOrder = 999;
	constexpr int32 StartMessageZOrder = 1000;

	APlayerCameraManager* StartCamera(const ATerrainWorld& World)
	{
		APlayerController* Controller = UGameplayStatics::GetPlayerController(&World, 0);
		return Controller != nullptr ? Controller->PlayerCameraManager.Get() : nullptr;
	}

	UGameViewportClient* StartViewport(const ATerrainWorld& World)
	{
		UWorld* Level = World.GetWorld();
		return Level != nullptr ? Level->GetGameViewport() : nullptr;
	}

	void StartBlackScreen(const ATerrainWorld& World, FTerrainWorldStart& Start)
	{
		APlayerCameraManager* Camera = StartCamera(World);
		if (!Start.bBlack && Camera != nullptr && World.LoadingSettings.bFade)
		{
			Camera->SetManualCameraFade(1.0f, FLinearColor::Black, false);
			Start.bBlack = true;
		}
	}

	// The brush of the black widget: alive as long as the module.
	const FSlateBrush* StartCoverBrush()
	{
		static const FSlateColorBrush Brush(FLinearColor::Black);
		return &Brush;
	}

	void HoldStartPawn(FTerrainWorldStart& Start, APawn* Pawn)
	{
		if (Pawn == nullptr || Start.HeldPawn.Get() == Pawn)
		{
			return;
		}
		if (UPawnMovementComponent* Movement = Pawn->GetMovementComponent())
		{
			Movement->StopMovementImmediately();
			Movement->Deactivate();
		}
		Start.HeldPawn = Pawn;
	}

	void ShowStartMessage(const ATerrainWorld& World, FTerrainWorldStart& Start)
	{
		UGameViewportClient* Viewport = StartViewport(World);
		if (Start.Message.IsValid() || Viewport == nullptr)
		{
			return;
		}
		Start.Message = SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Chargement du monde…")))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 28))
				.ColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.85f, 1.0f))
			];
		Viewport->AddViewportWidgetContent(Start.Message.ToSharedRef(), StartMessageZOrder);
	}
} // namespace

void BeginTerrainBlack(const ATerrainWorld& World, FTerrainWorldStart& Start)
{
	if (!World.LoadingSettings.bFade)
	{
		return;
	}
	UGameViewportClient* Viewport = StartViewport(World);
	if (!Start.Cover.IsValid() && Viewport != nullptr)
	{
		Start.Cover = SNew(SImage).Image(StartCoverBrush()).Visibility(EVisibility::HitTestInvisible);
		Viewport->AddViewportWidgetContent(Start.Cover.ToSharedRef(), StartCoverZOrder);
	}
	StartBlackScreen(World, Start);
}

void RemoveTerrainWidget(const ATerrainWorld& World, TSharedPtr<SWidget>& Widget)
{
	UGameViewportClient* Viewport = StartViewport(World);
	if (Widget.IsValid() && Viewport != nullptr)
	{
		Viewport->RemoveViewportWidgetContent(Widget.ToSharedRef());
	}
	Widget.Reset();
}

void BeginTerrainStart(FTerrainWorldStart& Start, const terrain::unreal::SessionParams& Params,
	double CoreOriginX, double CoreOriginZ)
{
	Start.StartSeconds = FPlatformTime::Seconds();
	Start.PlanetFile = Params.planetFile;
	Start.Before = FPlatformMemory::GetStats();
	// Nothing of Unreal but the allocation: the session is pure C++ (Bridge, terrain_core).
	Start.Build = std::async(std::launch::async, [Params, CoreOriginX, CoreOriginZ]()
		{
			const double Begin = FPlatformTime::Seconds();
			TPimplPtr<FTerrainWorldState> Built = MakePimpl<FTerrainWorldState>(Params);
			Built->Frame = Built->Session.frameAt(CoreOriginX, CoreOriginZ);
			Built->Loading.SessionSeconds = FPlatformTime::Seconds() - Begin;
			return Built;
		});
}

TPimplPtr<FTerrainWorldState> TickTerrainStart(ATerrainWorld& World, FTerrainWorldStart& Start,
	APawn* Pawn)
{
	StartBlackScreen(World, Start);
	HoldStartPawn(Start, Pawn);
	const double Elapsed = FPlatformTime::Seconds() - Start.StartSeconds;
	if (Elapsed > 0.0)
	{
		Start.WorstFrameMilliseconds = FMath::Max(Start.WorstFrameMilliseconds,
			static_cast<double>(FPlatformTime::ToMilliseconds(GGameThreadTime)));
	}
	if (Start.Build.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
	{
		if (Elapsed >= World.LoadingSettings.MessageDelaySeconds)
		{
			ShowStartMessage(World, Start);
		}
		return TPimplPtr<FTerrainWorldState>();
	}
	RemoveTerrainWidget(World, Start.Message);
	TPimplPtr<FTerrainWorldState> State = Start.Build.get(); // rethrows the error of the build
	State->Loading.bScreenBlack = Start.bBlack;
	State->Loading.Cover = MoveTemp(Start.Cover);
	State->Loading.HeldPawn = Start.HeldPawn;
	LogPlanet(State->Session.planet(), Start.PlanetFile, Start.Before);
	UE_LOG(LogTerrainEngine, Log,
		TEXT("Terrain World: the world built on a thread in %.3f s (planetary map, planet level, "
			 "hydrology, tiles), the game thread free meanwhile: worst frame %.1f ms"),
		State->Loading.SessionSeconds, Start.WorstFrameMilliseconds);
	return State;
}

void EndTerrainStart(ATerrainWorld& World, FTerrainWorldStart& Start)
{
	RemoveTerrainWidget(World, Start.Message);
	RemoveTerrainWidget(World, Start.Cover);
	APlayerCameraManager* Camera = StartCamera(World);
	if (Start.bBlack && Camera != nullptr)
	{
		Camera->StopCameraFade();
	}
	Start.bBlack = false;
	if (APawn* Held = Start.HeldPawn.Get())
	{
		if (UPawnMovementComponent* Movement = Held->GetMovementComponent())
		{
			Movement->Activate();
		}
	}
	Start.HeldPawn.Reset();
}

// While the world builds on its thread: false. Once built, the rest of the start on the game
// thread, then true: the streaming goes on from this frame.
bool ATerrainWorld::TickStart()
{
	FVector Focus = FVector::ZeroVector;
	APawn* Pawn = nullptr;
	FindFocus(Focus, Pawn);
	try
	{
		State = TickTerrainStart(*this, *Starting.Get(), Pawn);
	}
	catch (const std::exception& Exception)
	{
		EndTerrainStart(*this, *Starting.Get());
		Starting.Reset();
		State.Reset();
		UE_LOG(LogTerrainEngine, Error, TEXT("Terrain World: cannot start the streaming: %s"),
			*ToFString(Exception.what()));
		SetActorTickEnabled(false);
		return false;
	}
	if (!State.IsValid())
	{
		return false;
	}
	const double StartSeconds = Starting->StartSeconds;
	Starting.Reset();
	FinishStreamingStart(StartSeconds);
	return true;
}

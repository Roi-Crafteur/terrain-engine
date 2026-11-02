// A click on the dev map, or a point typed (agent 35): on land the pawn of the game, over water
// with no dry ground near the view of the tools above the water (Bridge/map_landing). The landing
// is decided on the thread of the map (clickLanding, a few thousand columns of the core at sea),
// the view goes there on the one teleport of the tools (ATerrainWorld::TeleportView: the screen
// black, the pawn held until the world around is shown, agent 38), then the tools of agent 36 give
// the pawn of the game back (ATerrainWorld::ReturnToGamePawn: the walkable spot near the view,
// checked on the collision of the tiles, watched for a second). When they refuse every spot near
// the dry ground of a wet click, the view goes back above the water at the click. A second after
// the pawn of the game is given back, the line of the check in the log: its movement, its speed,
// its feet against the water of the core. Only with TERRAIN_DEV_TOOLS: never in Shipping.
#include "TerrainDevMap.h"

#if TERRAIN_DEV_TOOLS

#include "Bridge/map_landing.hpp"
#include "Bridge/map_targets.hpp"
#include "Bridge/place_pose.hpp"
#include "Bridge/terrain_session.hpp"
#include "Bridge/unreal_frame.hpp"
#include "Bridge/walkable_ground.hpp"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpectatorPawn.h"
#include "HAL/PlatformTime.h"
#include "TerrainDevMapState.h"
#include "TerrainDevState.h"
#include "TerrainEngineLog.h"
#include "TerrainWorld.h"
#include "TerrainWorldState.h"
#include "terrain/generation/heightfield.hpp"
#include "terrain/world_size.hpp"

#include <cmath>
#include <optional>

namespace
{
	using TerrainWorldConversions::ToFString;
	using TerrainWorldConversions::ToUnrealPoint;

	// The line of the check, this long after the pawn of the game was given back.
	constexpr double CheckSeconds = 1.0;
	// Frames after TeleportView before its load is read.
	constexpr int32 SettleFrames = 2;
	// The landing of a click given up after this long (its column reads failed: the log says why).
	constexpr double DecideTimeoutSeconds = 30.0;
	// The water of the core looked for around the feet of the pawn, meters (as the tools do).
	constexpr double WaterAroundMeters = 3.0;
	// Faster than this a second after it was put down, the pawn is not still.
	constexpr double StillMetersPerSecond = 0.5;

	void ShowOnScreen(const FString& Message)
	{
		if (GEngine != nullptr)
		{
			GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Yellow, Message);
		}
	}
} // namespace

void UTerrainDevMapComponent::RequestClick(double X, double Z, double Heading, double Pitch,
	const FString& What)
{
	if (!State.IsValid())
	{
		return;
	}
	FTerrainMapClick Asked;
	Asked.X = X;
	Asked.Z = Z;
	Asked.Heading = Heading;
	Asked.Pitch = Pitch;
	Asked.What = What;
	Asked.Since = FPlatformTime::Seconds();
	State->Click = Asked;
	CloseDevMap();
	ShowLoading(true, X, Z);
	State->Cache->land(X, Z, TerrainDev::PawnRadiusMeters());
}

void UTerrainDevMapComponent::TickClick()
{
	if (!State.IsValid() || !State->Click.has_value())
	{
		return;
	}
	FTerrainMapClick& Asked = *State->Click;
	++Asked.Frames;
	const double Now = FPlatformTime::Seconds();
	if (Asked.Step == FTerrainMapClick::EStep::Loading)
	{
		TickClickLoading();
		return;
	}
	if (Asked.Step == FTerrainMapClick::EStep::Watching)
	{
		if (Now - Asked.Since >= CheckSeconds)
		{
			LogClickCheck();
			State->Click.reset();
		}
		return;
	}
	const std::optional<terrain::unreal::MapLanding> Landed = State->Cache->takeLanding();
	if (!Landed.has_value() || Landed->clickX != Asked.X || Landed->clickZ != Asked.Z)
	{
		if (Now - Asked.Since > DecideTimeoutSeconds)
		{
			ShowLoading(false);
			SetMessage(FString::Printf(TEXT("No landing for %s: %s"), *Asked.What,
				*ToFString(State->Cache->lastError())));
			State->Click.reset();
		}
		return; // not decided yet, or the landing of an older click
	}
	ShowLoading(false);
	Asked.Landing = *Landed;
	ATerrainWorld* Terrain = TerrainWorld();
	if (Terrain == nullptr || !Terrain->TeleportView(Landed->x, Landed->z, Asked.Heading, Asked.Pitch,
								  terrain::unreal::kMapEyeAboveGround, ETerrainEyeMode::Ground))
	{
		SetMessage(FString::Printf(TEXT("The teleport to %s was refused (the tools of the developers "
										"are off, or the world is not playing: the log says why)"),
			*Asked.What));
		State->Click.reset();
		return;
	}
	const FString Said =
		ToFString(terrain::unreal::landingText(*Landed, TerrainDev::PawnRadiusMeters()));
	SetMessage(FString::Printf(TEXT("%s: %s"), *Asked.What, *Said));
	if (Landed->kind == terrain::unreal::MapLandingKind::OverWater)
	{
		ShowOnScreen(Said);
		State->Click.reset();
		return;
	}
	Asked.Step = FTerrainMapClick::EStep::Loading;
	Asked.Since = Now;
	Asked.Frames = 0;
}

// The view of the tools at the dry ground loaded: the pawn of the game given back by the tools of
// agent 36 (they refuse while the view loads); if they refuse every spot near the dry ground of a
// wet click, the view goes back above the water at the click.
void UTerrainDevMapComponent::TickClickLoading()
{
	FTerrainMapClick& Asked = *State->Click;
	ATerrainWorld* Terrain = TerrainWorld();
	const double Now = FPlatformTime::Seconds();
	if (Terrain == nullptr)
	{
		State->Click.reset();
		return;
	}
	if (Asked.Frames < SettleFrames || Terrain->IsViewLoading())
	{
		if (Now - Asked.Since > TerrainDev::ShotTimeoutSeconds())
		{
			SetMessage(TEXT("The view did not load in time: the view of the tools stays"));
			State->Click.reset();
		}
		return;
	}
	if (Terrain->ReturnToGamePawn())
	{
		Asked.Step = FTerrainMapClick::EStep::Watching;
		Asked.Since = Now;
		Asked.Frames = 0;
		return;
	}
	const std::optional<terrain::unreal::MapLanding> Back =
		terrain::unreal::landingFallback(Asked.Landing);
	if (Back.has_value() &&
		Terrain->TeleportView(Back->x, Back->z, Asked.Heading, Asked.Pitch,
			terrain::unreal::kMapEyeAboveGround, ETerrainEyeMode::Ground))
	{
		const FString Said = FString::Printf(TEXT("no spot near the dry ground holds the pawn: back "
												  "%s"),
			*ToFString(terrain::unreal::landingText(*Back, TerrainDev::PawnRadiusMeters())));
		SetMessage(Said);
		ShowOnScreen(Said);
	}
	else
	{
		SetMessage(TEXT("The view of the tools stays (the message of the tools says why)"));
	}
	State->Click.reset();
}

// The line of the check, a second after the pawn of the game was given back: its movement and its
// speed, its feet against the water of the core around them, how far from the click.
void UTerrainDevMapComponent::LogClickCheck()
{
	const FTerrainMapClick& Asked = *State->Click;
	ATerrainWorld* Terrain = TerrainWorld();
	const APlayerController* Controller =
		GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
	APawn* Pawn = Controller != nullptr ? ToRawPtr(Controller->GetPawn()) : nullptr;
	if (Terrain == nullptr || !Terrain->State.IsValid() || Pawn == nullptr)
	{
		return;
	}
	float Radius = 0.0f;
	float HalfHeight = 0.0f;
	Pawn->GetSimpleCollisionCylinder(Radius, HalfHeight);
	const FVector Location = Pawn->GetActorLocation();
	const terrain::unreal::CorePoint Feet = terrain::unreal::toCore(Terrain->State->Frame,
		ToUnrealPoint(Location - FVector(0.0, 0.0, HalfHeight)));
	const ACharacter* Character = Cast<ACharacter>(Pawn);
	const UCharacterMovementComponent* Movement =
		Character != nullptr ? Character->GetCharacterMovement() : nullptr;
	const bool bWalking = Movement != nullptr && Movement->MovementMode == MOVE_Walking;
	const double Speed = Pawn->GetVelocity().Size() / 100.0;
	const terrain::Heightfield& Field = Terrain->State->Session.tiles().heightfield();
	const std::optional<double> Water = terrain::unreal::waterSurfaceAround(Feet.x, Feet.z,
		WaterAroundMeters, [&Field](double X, double Z) { return Field.sampleAt(X, Z); });
	const terrain::WorldSize& Size = State->World;
	const double Away = std::hypot(terrain::nearestImage(Feet.x, Asked.X, Size.x) - Asked.X,
		terrain::nearestImage(Feet.z, Asked.Z, Size.z) - Asked.Z);
	const FString WaterText = Water.has_value()
		? FString::Printf(TEXT("%.2f m above the water at %.2f m"), Feet.y - *Water, *Water)
		: FString::Printf(TEXT("no water within %.0f m"), WaterAroundMeters);
	const FString Line = FString::Printf(TEXT("%.0f s after the click: %s, %s, %.2f m/s, feet at "
											  "%.2f m (%s), %.1f m from the click"),
		CheckSeconds, Cast<ASpectatorPawn>(Pawn) != nullptr ? TEXT("the view of the tools")
															: TEXT("the pawn of the game"),
		Movement != nullptr ? *Movement->GetMovementName() : TEXT("no CharacterMovement"), Speed,
		Feet.y, *WaterText, Away);
	if (bWalking && Speed < StillMetersPerSecond && (!Water.has_value() || Feet.y >= *Water))
	{
		UE_LOG(LogTerrainEngine, Log, TEXT("Terrain Dev Map: %s: OK"), *Line);
	}
	else
	{
		UE_LOG(LogTerrainEngine, Warning, TEXT("Terrain Dev Map: %s: NOT walking still above the "
											   "water"),
			*Line);
	}
}

#endif // TERRAIN_DEV_TOOLS

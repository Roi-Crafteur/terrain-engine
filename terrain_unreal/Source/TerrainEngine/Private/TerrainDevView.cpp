// The views of the tools of the developers (agent 36): the one teleport (TeleportView, called by
// Terrain.Goto and by the dev map), the flying pawn of the tools that holds the eye exactly where
// asked, the wait for the tiles around it, the pawn of the game given back, and the commands
// Terrain.Goto, Look, Eye, Pawn, Save and Info. The poses come from Bridge/place_pose.
#include "TerrainDevTools.h"

#if TERRAIN_DEV_TOOLS

#include "Engine/World.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpectatorPawn.h"
#include "HAL/PlatformTime.h"
#include "TerrainDevState.h"
#include "TerrainWorld.h"
#include "TerrainWorldState.h"

#include <exception>
#include <optional>

namespace
{
	using TerrainDev::ToText;

	// The eye of Terrain.Goto x z without one: standing on the ground (as terrain_export_view).
	constexpr double StandingEye = 1.8;
	// The pawn of the tools flies at 50 m/s (the floating pawn of Unreal: 12 m/s).
	constexpr float DevPawnSpeed = 5000.0f;

	ETerrainEyeMode EyeModeOf(terrain::dev::EyeMode Mode)
	{
		return Mode == terrain::dev::EyeMode::Ground ? ETerrainEyeMode::Ground
													 : ETerrainEyeMode::Altitude;
	}
} // namespace

bool UTerrainDevTools::TeleportView(double X, double Z, double Heading, double Pitch, double Eye,
	ETerrainEyeMode EyeMode)
{
	if (!Dev.IsValid() || World == nullptr || !World->State.IsValid())
	{
		return false;
	}
	const terrain::HeightSample Sample = World->State->Session.tiles().heightfield().sampleAt(X, Z);
	const terrain::dev::EyeMode Mode = EyeMode == ETerrainEyeMode::Ground
		? terrain::dev::EyeMode::Ground
		: terrain::dev::EyeMode::Altitude;
	return PlaceView(terrain::unreal::viewPose(X, Z, Heading, Pitch, Eye, Mode,
		terrain::unreal::columnTop(Sample)));
}

// The pawn of the tools, its eye at the eye of the pose (the image of the point nearest to the
// view of the player: the tiles follow it there), the ground under it generated now, then held
// still until the tiles around are loaded (TickHold).
bool UTerrainDevTools::PlaceView(const terrain::unreal::ViewPose& Pose)
{
	APlayerController* PlayerController = Controller();
	APawn* Flying = PlayerController != nullptr ? EnsureDevPawn(*PlayerController) : nullptr;
	if (Flying == nullptr)
	{
		TerrainDev::Warn(TEXT("no player to move"));
		return false;
	}
	terrain::unreal::CorePoint EyePoint = Pose.eye;
	terrain::unreal::ViewPose Here;
	if (CurrentPose(Here))
	{
		const terrain::WorldSize& Size = World->State->Session.tiles().world();
		EyePoint.x = terrain::nearestImage(EyePoint.x, Here.eye.x, Size.x);
		EyePoint.z = terrain::nearestImage(EyePoint.z, Here.eye.z, Size.z);
	}
	const terrain::unreal::UnrealPoint Target =
		terrain::unreal::toUnreal(World->State->Frame, EyePoint);
	const FVector ViewOffset = Flying->GetPawnViewLocation() - Flying->GetActorLocation();
	Flying->SetActorLocation(FVector(Target.x, Target.y, Target.z) - ViewOffset, false, nullptr,
		ETeleportType::TeleportPhysics);
	if (UPawnMovementComponent* Movement = Flying->GetMovementComponent())
	{
		Movement->StopMovementImmediately();
	}
	PlayerController->SetControlRotation(FRotator(Pose.pitch, Pose.yaw, 0.0));
	const double Start = FPlatformTime::Seconds();
	// The eye stays exactly where asked: no snap onto the ground.
	const bool bSnap = World->bSnapPawnToGround;
	World->bSnapPawnToGround = false;
	World->WarmUpAround(*Flying);
	World->bSnapPawnToGround = bSnap;
	if (!Dev->Hold.has_value())
	{
		PlayerController->SetIgnoreMoveInput(true);
	}
	Dev->Hold = FTerrainDevHold{Start,
		terrain::unreal::ShotWaiter({0.0, 0.0, TerrainDev::ShotTimeoutSeconds())}};
	TerrainDev::Log(FString::Printf(TEXT("view at x %.2f, z %.2f, eye %.2f m, heading %.1f, "
		"pitch %.1f; the ground ready in %.2f s"), Pose.eye.x, Pose.eye.z, Pose.eye.y,
		terrain::unreal::headingOfYaw(Pose.yaw), Pose.pitch, FPlatformTime::Seconds() - Start));
	return true;
}

APawn* UTerrainDevTools::EnsureDevPawn(APlayerController& PlayerController)
{
	if (IsValid(DevPawn))
	{
		if (PlayerController.GetPawn() != DevPawn)
		{
			PlayerController.Possess(DevPawn);
		}
		return DevPawn;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	APawn* Previous = PlayerController.GetPawn();
	const FVector Location = Previous != nullptr ? Previous->GetActorLocation() : FVector::ZeroVector;
	ASpectatorPawn* Spectator = World->GetWorld()->SpawnActor<ASpectatorPawn>(
		ASpectatorPawn::StaticClass(), Location, FRotator::ZeroRotator, Params);
	if (Spectator == nullptr)
	{
		return nullptr;
	}
	if (UFloatingPawnMovement* Movement = Cast<UFloatingPawnMovement>(Spectator->GetMovementComponent()))
	{
		Movement->MaxSpeed = DevPawnSpeed;
	}
	GamePawn = Previous;
	PlayerController.Possess(Spectator);
	DevPawn = Spectator;
	return Spectator;
}

void UTerrainDevTools::TickHold()
{
	if (!Dev->Hold.has_value())
	{
		return;
	}
	const double Elapsed = FPlatformTime::Seconds() - Dev->Hold->Start;
	const terrain::unreal::StreamingState State =
		terrain::unreal::tileState(World->State->Session.streamer());
	const terrain::unreal::ShotWait Wait = Dev->Hold->Waiter.update(Elapsed, State);
	if (Wait == terrain::unreal::ShotWait::Waiting)
	{
		return;
	}
	TerrainDev::Log(FString::Printf(TEXT("%s after %.1f s (%d of %d tiles shown, %d pending): the "
		"pawn moves again"), Wait == terrain::unreal::ShotWait::Ready
			? TEXT("the tiles around the view are loaded")
			: TEXT("NOT every tile around the view is loaded"),
		Elapsed, static_cast<int32>(State.shownTiles), static_cast<int32>(State.selectedTiles),
		static_cast<int32>(State.pendingTiles)));
	EndHold();
}

void UTerrainDevTools::EndHold()
{
	if (!Dev->Hold.has_value())
	{
		return;
	}
	if (APlayerController* PlayerController = Controller())
	{
		PlayerController->SetIgnoreMoveInput(false);
	}
	Dev->Hold.reset();
}

bool UTerrainDevTools::ReturnToGamePawn(bool bUnderView)
{
	APlayerController* PlayerController = Controller();
	if (PlayerController == nullptr || !IsValid(DevPawn) || !World->State.IsValid())
	{
		return false;
	}
	EndHold();
	const FVector ViewLocation = DevPawn->GetPawnViewLocation();
	const FRotator Rotation = PlayerController->GetControlRotation();
	APawn* Game = GamePawn.Get();
	if (IsValid(Game))
	{
		if (bUnderView)
		{
			const double GroundZ = World->State->Session.groundZ(World->State->Frame,
				TerrainWorldConversions::ToUnrealPoint(ViewLocation));
			Game->SetActorLocation(FVector(ViewLocation.X, ViewLocation.Y,
				GroundZ + Game->GetSimpleCollisionHalfHeight() + 50.0), false, nullptr,
				ETeleportType::TeleportPhysics);
		}
		PlayerController->Possess(Game);
		PlayerController->SetControlRotation(Rotation);
		if (UPawnMovementComponent* Movement = Game->GetMovementComponent())
		{
			Movement->StopMovementImmediately();
		}
		World->WarmUpAround(*Game);
	}
	else
	{
		TerrainDev::Warn(TEXT("the player had no pawn of the game: it keeps none"));
		PlayerController->UnPossess();
	}
	DevPawn->Destroy();
	DevPawn = nullptr;
	GamePawn = nullptr;
	return true;
}

void UTerrainDevTools::RunGoto(const std::vector<std::string>& Args)
{
	std::string Error;
	const std::optional<terrain::unreal::GotoCommand> Command = terrain::unreal::parseGoto(Args, Error);
	if (!Command)
	{
		TerrainDev::Warn(ToText(Error));
		return;
	}
	if (!Command->place.empty())
	{
		terrain::dev::PlacesFile Places;
		const terrain::dev::Place* Found =
			LoadPlacesFile(Places, false) ? Places.find(Command->place) : nullptr;
		if (Found == nullptr)
		{
			TerrainDev::Warn(FString::Printf(TEXT("no place %s in %s"), *ToText(Command->place),
				*PlacesPath()));
			return;
		}
		if (Found->planet && !HasPlanet())
		{
			TerrainDev::Warn(FString::Printf(TEXT("%s needs the planet file (rivers, falls, lakes): "
				"this world has none"), *ToText(Found->name)));
		}
		TeleportView(Found->x, Found->z, Found->heading, Found->pitch, Found->eye,
			EyeModeOf(Found->eyeMode));
		return;
	}
	terrain::unreal::ViewPose Here;
	const bool bHere = CurrentPose(Here);
	const terrain::unreal::EyeArgument EyeSetting = Command->eye.value_or(
		terrain::unreal::EyeArgument{StandingEye, terrain::dev::EyeMode::Ground});
	TeleportView(Command->x, Command->z, bHere ? terrain::unreal::headingOfYaw(Here.yaw) : 0.0,
		bHere ? Here.pitch : 0.0, EyeSetting.value, EyeModeOf(EyeSetting.mode));
}

void UTerrainDevTools::RunLook(const std::vector<std::string>& Args)
{
	std::string Error;
	const std::optional<terrain::unreal::LookCommand> Command = terrain::unreal::parseLook(Args, Error);
	APlayerController* PlayerController = Controller();
	if (!Command || PlayerController == nullptr)
	{
		TerrainDev::Warn(ToText(Command ? std::string("no player") : Error));
		return;
	}
	PlayerController->SetControlRotation(
		FRotator(Command->pitch, terrain::unreal::yawOfHeading(Command->heading), 0.0));
}

void UTerrainDevTools::RunEye(const std::vector<std::string>& Args)
{
	std::string Error;
	const std::optional<terrain::unreal::EyeArgument> EyeSetting =
		terrain::unreal::parseEye(Args, Error);
	terrain::unreal::ViewPose Here;
	if (!EyeSetting || !CurrentPose(Here))
	{
		TerrainDev::Warn(ToText(EyeSetting ? std::string("no camera") : Error));
		return;
	}
	TeleportView(Here.eye.x, Here.eye.z, terrain::unreal::headingOfYaw(Here.yaw), Here.pitch,
		EyeSetting->value, EyeModeOf(EyeSetting->mode));
}

void UTerrainDevTools::RunPawn()
{
	if (!ReturnToGamePawn(true))
	{
		TerrainDev::Warn(TEXT("the player already has the pawn of the game"));
	}
}

void UTerrainDevTools::RunSave(const std::vector<std::string>& Args)
{
	std::string Error;
	const std::optional<terrain::unreal::SaveCommand> Command = terrain::unreal::parseSave(Args, Error);
	terrain::unreal::ViewPose Here;
	terrain::dev::PlacesFile Places;
	if (!Command || !CurrentPose(Here) || !LoadPlacesFile(Places, true))
	{
		TerrainDev::Warn(ToText(Command ? std::string("no camera, or no places file") : Error));
		return;
	}
	try
	{
		const terrain::dev::Place Saved = terrain::unreal::placeOfView(Command->name,
			Command->note, Here, HasPlanet(), World->State->Session.tiles().world());
		Places.put(Saved);
		terrain::dev::savePlaces(TerrainDev::ToPath(PlacesPath()), Places);
		TerrainDev::Log(FString::Printf(TEXT("place %s saved in %s: x %.2f, z %.2f, heading "
			"%.2f, pitch %.2f, eye %.2f m (altitude)"), *ToText(Saved.name), *PlacesPath(),
			Saved.x, Saved.z, Saved.heading, Saved.pitch, Saved.eye));
	}
	catch (const std::exception& Failure)
	{
		TerrainDev::Warn(ToText(Failure.what()));
	}
}

void UTerrainDevTools::RunInfo(const std::vector<std::string>& Args)
{
	TArray<FString> Lines;
	if (!Args.empty() && Args[0] == "here")
	{
		ColumnLines(Lines);
	}
	else
	{
		AimedLines(Lines);
	}
	for (const FString& Line : Lines)
	{
		TerrainDev::Log(Line);
	}
}

#endif // TERRAIN_DEV_TOOLS

// The views of the tools of the developers (agent 36): TeleportView (called by Terrain.Goto and
// by the dev map) on the one teleport of the world (ATerrainWorld::BeginTeleport, the loading of
// agent 38), the flying pawn of the tools that holds the eye exactly where asked, and the
// commands Terrain.Goto, Look, Eye, Save and Info (the pawn of the game given back:
// TerrainDevPawn.cpp). The poses come from Bridge/place_pose.
#include "TerrainDevTools.h"

#if TERRAIN_DEV_TOOLS

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpectatorPawn.h"
#include "HAL/PlatformTime.h"
#include "TerrainDevState.h"
#include "TerrainWorld.h"
#include "TerrainWorldLoading.h"
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
	return PlaceView(X, Z, Heading, Pitch, Eye, EyeMode == ETerrainEyeMode::Ground
		? terrain::dev::EyeMode::Ground
		: terrain::dev::EyeMode::Altitude);
}

// The pawn of the tools taken by the player, then the one teleport of the world
// (ATerrainWorld::BeginTeleport, the loading of agent 38) with its eye: the screen black at once,
// the eye at its altitude at once (Altitude) or above the ground or the water once the ground is
// read on a thread (Ground), the pawn held until the collision under it, the screen back once
// the world around is shown. At the image of the point nearest to the view of the player.
bool UTerrainDevTools::PlaceView(double X, double Z, double Heading, double Pitch, double Eye,
	terrain::dev::EyeMode Mode)
{
	APlayerController* PlayerController = Controller();
	APawn* Flying = PlayerController != nullptr ? EnsureDevPawn(*PlayerController) : nullptr;
	if (Flying == nullptr)
	{
		TerrainDev::Warn(TEXT("no player to move"));
		return false;
	}
	FTerrainTeleport Teleport;
	Teleport.X = X;
	Teleport.Z = Z;
	terrain::unreal::ViewPose Here;
	if (CurrentPose(Here))
	{
		const terrain::WorldSize& Size = World->State->Session.tiles().world();
		Teleport.X = terrain::nearestImage(X, Here.eye.x, Size.x);
		Teleport.Z = terrain::nearestImage(Z, Here.eye.z, Size.z);
	}
	Teleport.Heading = Heading;
	Teleport.Pitch = Pitch; // negative down (DEV_TOOLS.md, the contract)
	Teleport.EyeMeters = Eye;
	Teleport.bEyeAboveStand = Mode == terrain::dev::EyeMode::Ground;
	if (!World->BeginTeleport(*Flying, Teleport))
	{
		return false;
	}
	// A cut: the motion blur and the temporal history start again from the new view.
	if (PlayerController->PlayerCameraManager != nullptr)
	{
		PlayerController->PlayerCameraManager->SetGameCameraCutThisFrame();
	}
	TerrainDev::Log(FString::Printf(TEXT("view asked at x %.2f, z %.2f, eye %.2f m %s, heading "
		"%.1f, pitch %.1f: shown once the world around is loaded"), X, Z, Eye,
		Mode == terrain::dev::EyeMode::Ground ? TEXT("above the ground or the water")
											  : TEXT("of altitude"), Heading, Pitch));
	return true;
}

APawn* UTerrainDevTools::EnsureDevPawn(APlayerController& PlayerController)
{
	if (IsValid(DevPawn))
	{
		if (ToRawPtr(PlayerController.GetPawn()) != DevPawn.Get())
		{
			PlayerController.Possess(DevPawn.Get());
		}
		World->WarmedPawn = DevPawn.Get(); // its ground comes with the load, never generated now
		return DevPawn.Get();
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	APawn* Previous = ToRawPtr(PlayerController.GetPawn());
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
	// The tick of the world would generate the ground of a new focus pawn at once (WarmUpAround):
	// the ground of a view comes with its load instead.
	World->WarmedPawn = Spectator;
	return Spectator;
}

// The load of the last teleport holds the view: the screen black or the pawn held (LoadPhase).
bool UTerrainDevTools::IsViewLoading() const
{
	if (!Dev.IsValid() || World == nullptr || !World->State.IsValid())
	{
		return false;
	}
	const TOptional<terrain::unreal::LoadPhase>& Phase = World->State->Loading.Phase;
	return Phase.IsSet() && (Phase->fading() || Phase->holding());
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
		TerrainDev::ToRotator(terrain::unreal::viewRotation(Command->heading, Command->pitch)));
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
	if (!World->State.IsValid())
	{
		TerrainDev::Warn(TEXT("no world playing"));
		return;
	}
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

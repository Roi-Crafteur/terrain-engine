// The pawn of the game given back by the tools of the developers (agent 36: Terrain.Pawn,
// ATerrainWorld::ReturnToGamePawn; the contract in terrain_unreal/DEV_TOOLS.md). It goes down on
// walkable ground near the view, never on the bottom of water nor on a slope it slides down:
// - the candidates of the heightmap (Bridge/walkable_ground: walkableCandidates), nearest first;
// - each checked on the collision of the tiles: five rays down, at its center and around the
//   capsule (checkStand: the normals under the walkable angle less 5 degrees, a step at most,
//   near the heightmap, above the water of the core);
// - the pawn put on the first that passes, then watched: not on the ground two frames later, or
//   moved more than 0.5 m in a second, and it goes to the next one (8 at most).
#include "TerrainDevTools.h"

#if TERRAIN_DEV_TOOLS

#include "Bridge/walkable_ground.hpp"
#include "CollisionQueryParams.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "TerrainDevState.h"
#include "TerrainWorld.h"
#include "TerrainWorldState.h"

#include <exception>

namespace
{
	using TerrainDev::ToText;
	using terrain::unreal::StandRefusal;

	// The pawn of the game goes down on slopes this much under the walkable angle of its
	// CharacterMovement; the walkable angle and the step of a pawn that is no Character.
	constexpr double WalkableMarginDegrees = 5.0;
	constexpr double DefaultWalkableAngle = 44.765;
	constexpr double DefaultStepCm = 45.0;
	// The candidates of the heightmap checked by rays, and the spots that pass them kept to try.
	constexpr std::size_t MaxCandidates = 64;
	constexpr int32 MaxLandingTries = 8;
	// The rays start this far above the heightmap and end this far under it.
	constexpr double RayReachCm = 2000.0;
	// The water of the core looked for this far around a candidate, meters.
	constexpr double WaterAroundMeters = 3.0;
	// The pawn put this far above the highest hit; it slid when it moved more than SlideCm.
	constexpr double ClearanceCm = 2.0;
	constexpr double SlideCm = 50.0;
	constexpr double WatchSeconds = 1.0;

	double SlopeDegrees(double Slope)
	{
		return FMath::RadiansToDegrees(FMath::Atan(Slope));
	}

	// A message on the screen (8 s) and in the log.
	void ShowAndLog(const FString& Message)
	{
		TerrainDev::Warn(Message);
		if (GEngine != nullptr)
		{
			GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Yellow, Message);
		}
	}

	// The walkable angle and the step of the pawn: those of its CharacterMovement. Written in the
	// log with its capsule (agent 41: the tunnels dug block by block are measured for them).
	void PawnLimits(APawn& Pawn, double& OutAngle, double& OutStepCm)
	{
		OutAngle = DefaultWalkableAngle;
		OutStepCm = DefaultStepCm;
		if (const ACharacter* Character = Cast<ACharacter>(&Pawn))
		{
			if (const UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
			{
				OutAngle = Movement->GetWalkableFloorAngle();
				OutStepCm = Movement->MaxStepHeight;
			}
		}
		float Radius = 0.0f;
		float HalfHeight = 0.0f;
		Pawn.GetSimpleCollisionCylinder(Radius, HalfHeight);
		TerrainDev::Log(FString::Printf(TEXT("the pawn of the game: capsule radius %.1f cm, half "
			"height %.1f cm; walkable floor angle %.3f degrees, step %.1f cm"), Radius, HalfHeight,
			OutAngle, OutStepCm));
	}

	// The pawn at Location, still, walking.
	void PutDown(APawn& Pawn, const FVector& Location)
	{
		Pawn.SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		if (UPawnMovementComponent* Movement = Pawn.GetMovementComponent())
		{
			Movement->StopMovementImmediately();
		}
		if (ACharacter* Character = Cast<ACharacter>(&Pawn))
		{
			if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
			{
				Movement->SetMovementMode(MOVE_Walking);
			}
		}
	}

	// Whether the pawn stands where it was put: on the ground two frames later, still a second
	// later. Empty: it stands (or not decided yet); otherwise why it slid.
	FString SlideOf(APawn& Pawn, const FTerrainDevLanding& Landing, double Now)
	{
		if (Landing.Frames == 2)
		{
			const ACharacter* Character = Cast<ACharacter>(&Pawn);
			const UCharacterMovementComponent* Movement =
				Character != nullptr ? Character->GetCharacterMovement() : nullptr;
			if (Movement != nullptr && !Movement->IsMovingOnGround())
			{
				return TEXT("not on the ground two frames after it was put down");
			}
		}
		const double Moved = FVector::Dist(Pawn.GetActorLocation(), Landing.Spots[Landing.Index]);
		if (Now - Landing.PlacedAt >= WatchSeconds && Moved > SlideCm)
		{
			return FString::Printf(TEXT("moved %.1f m in a second"), Moved / 100.0);
		}
		return FString();
	}
} // namespace

void UTerrainDevTools::RunPawn()
{
	if (!IsValid(DevPawn))
	{
		TerrainDev::Warn(TEXT("the player already has the pawn of the game"));
		return;
	}
	ReturnToGamePawn(true); // over water or without walkable ground: refused, with its message
}

bool UTerrainDevTools::ReturnToGamePawn(bool bUnderView)
{
	APlayerController* PlayerController = Controller();
	if (PlayerController == nullptr || !IsValid(DevPawn) || !World->State.IsValid())
	{
		return false;
	}
	const FVector ViewLocation = DevPawn->GetPawnViewLocation();
	const FRotator Rotation = PlayerController->GetControlRotation();
	APawn* Game = GamePawn.Get();
	if (bUnderView && IsViewLoading())
	{
		// The load of the view still holds the pawn and would put it back onto the heightmap.
		ShowAndLog(TEXT("Terrain.Pawn: the view is still loading: the pawn of the game is given "
			"back once it is shown"));
		return false;
	}
	TArray<FVector> Spots;
	if (bUnderView && IsValid(Game) &&
		(OverWater(ViewLocation) || !FindGameGround(*Game, ViewLocation, Spots)))
	{
		return false; // nothing changed: the view of the tools stays
	}
	if (IsValid(Game))
	{
		if (bUnderView)
		{
			PutDown(*Game, Spots[0]);
		}
		PlayerController->Possess(Game);
		PlayerController->SetControlRotation(Rotation);
		if (UPawnMovementComponent* Movement = Game->GetMovementComponent())
		{
			Movement->StopMovementImmediately();
		}
		// Put on the collision itself: no snap onto the heightmap, which may lie above it.
		const bool bSnap = World->bSnapPawnToGround;
		World->bSnapPawnToGround = !bUnderView && bSnap;
		try
		{
			World->WarmUpAround(*Game);
		}
		catch (const std::exception& Failure)
		{
			World->StopStreaming(ToText(Failure.what()));
		}
		World->bSnapPawnToGround = bSnap;
		if (bUnderView)
		{
			StartLanding(*Game, MoveTemp(Spots));
		}
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

// The column under the view holds water: said on the screen and in the log (the pawn of the game
// is never put on the bottom).
bool UTerrainDevTools::OverWater(const FVector& ViewLocation) const
{
	const terrain::unreal::CorePoint Under = terrain::unreal::toCore(World->State->Frame,
		TerrainWorldConversions::ToUnrealPoint(ViewLocation));
	if (!terrain::unreal::columnHasWater(
			World->State->Session.tiles().heightfield().sampleAt(Under.x, Under.z)))
	{
		return false;
	}
	ShowAndLog(TEXT("Terrain.Pawn: over water (sea, lake or river): the pawn of the game is not "
		"put down here, the view of the tools stays; move over dry ground first"));
	return true;
}

// The spots near the view where the pawn of the game stands on the collision of the tiles, the
// nearest first (MaxLandingTries at most), said on the screen when the first is not right under
// the view. False, said on the screen and in the log with the reason, when there is none.
bool UTerrainDevTools::FindGameGround(APawn& Game, const FVector& ViewLocation,
	TArray<FVector>& OutSpots) const
{
	double WalkableAngle = 0.0;
	double StepCm = 0.0;
	PawnLimits(Game, WalkableAngle, StepCm);
	terrain::unreal::WalkableSearch Search;
	Search.radius = TerrainDev::PawnRadiusMeters();
	Search.maxSlope = terrain::unreal::walkableSlope(WalkableAngle, WalkableMarginDegrees);
	const terrain::TerrainTiles& Tiles = World->State->Session.tiles();
	const terrain::Heightfield& Field = Tiles.heightfield();
	// The top of the rock the tiles draw: behind a face (a wall, a sea cliff) the heightmap lies in
	// the rock, up to half the height of the face below the ground the rays find.
	const terrain::unreal::GroundHeight Height = [&Tiles](double X, double Z)
	{ return static_cast<double>(Tiles.groundAt(X, Z)); };
	const terrain::unreal::CorePoint Under = terrain::unreal::toCore(World->State->Frame,
		TerrainWorldConversions::ToUnrealPoint(ViewLocation));
	const terrain::unreal::WalkableCandidates Candidates = terrain::unreal::walkableCandidates(
		Under.x, Under.z, Search, Height, [&Field](double X, double Z)
		{ return terrain::unreal::columnHasWater(Field.sampleAt(X, Z)); }, MaxCandidates);
	const double SteepDegrees =
		SlopeDegrees(terrain::unreal::groundSlope(Under.x, Under.z, Search.probe, Height));
	if (Candidates.spots.empty())
	{
		ShowAndLog(FString::Printf(TEXT("Terrain.Pawn: no walkable ground within %.0f m: %s (%.0f "
			"degrees under the view, the pawn walks up to %.0f); the pawn of the game is not put "
			"down here, the view of the tools stays"), Search.radius,
			*ToText(terrain::unreal::walkableReasonText(Candidates.reason)), SteepDegrees,
			SlopeDegrees(Search.maxSlope)));
		return false;
	}
	terrain::unreal::StandLimits Limits;
	Limits.minNormalUp = terrain::unreal::walkableNormalUp(WalkableAngle, WalkableMarginDegrees);
	Limits.maxStep = StepCm / 100.0;
	int32 Refused[6] = {};
	double FirstDistance = 0.0;
	for (const terrain::unreal::WalkableSpot& Spot : Candidates.spots)
	{
		FVector Location;
		const StandRefusal Refusal = ProbeSpot(Game, Spot, Limits, Location);
		++Refused[static_cast<int32>(Refusal)];
		if (Refusal == StandRefusal::None)
		{
			FirstDistance = OutSpots.IsEmpty() ? Spot.distance : FirstDistance;
			OutSpots.Add(Location);
			if (OutSpots.Num() >= MaxLandingTries)
			{
				break;
			}
		}
	}
	if (OutSpots.IsEmpty())
	{
		ShowAndLog(FString::Printf(TEXT("Terrain.Pawn: none of the %d walkable spots of the "
			"heightmap within %.0f m holds the pawn on the collision of the tiles (%d too steep, "
			"%d with a step, %d far from the heightmap, %d under water, %d without collision); the "
			"pawn of the game is not put down here, the view of the tools stays"),
			static_cast<int32>(Candidates.spots.size()), Search.radius,
			Refused[static_cast<int32>(StandRefusal::TooSteep)],
			Refused[static_cast<int32>(StandRefusal::Step)],
			Refused[static_cast<int32>(StandRefusal::OffHeightmap)],
			Refused[static_cast<int32>(StandRefusal::UnderWater)],
			Refused[static_cast<int32>(StandRefusal::NoCollision)]));
		return false;
	}
	if (FirstDistance > 0.0)
	{
		ShowAndLog(FString::Printf(TEXT("Terrain.Pawn: the ground under the view does not hold "
			"the pawn (%.0f degrees on the heightmap): the pawn of the game is put %.0f m away"),
			SteepDegrees, FirstDistance));
	}
	return true;
}

// One candidate on the collision of the tiles: the rays of its center and of four points around
// the capsule (the pawns ignored), and where the pawn goes when it stands there.
StandRefusal UTerrainDevTools::ProbeSpot(APawn& Game, const terrain::unreal::WalkableSpot& Spot,
	const terrain::unreal::StandLimits& Limits, FVector& OutLocation) const
{
	float Radius = 0.0f;
	float HalfHeight = 0.0f;
	Game.GetSimpleCollisionCylinder(Radius, HalfHeight);
	const terrain::unreal::UnrealFrame& Frame = World->State->Frame;
	const terrain::unreal::UnrealPoint Center =
		terrain::unreal::toUnreal(Frame, {Spot.x, Spot.ground, Spot.z});
	const FVector Offsets[5] = {FVector::ZeroVector, FVector(Radius, 0.0, 0.0),
		FVector(-Radius, 0.0, 0.0), FVector(0.0, Radius, 0.0), FVector(0.0, -Radius, 0.0)};
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TerrainDevPawn), true);
	Params.AddIgnoredActor(&Game);
	if (const APawn* Flying = DevPawn.Get())
	{
		Params.AddIgnoredActor(Flying);
	}
	terrain::unreal::FloorProbe Probes[5];
	for (int32 I = 0; I < 5; ++I)
	{
		const FVector Top = FVector(Center.x, Center.y, Center.z + RayReachCm) + Offsets[I];
		const FVector Bottom = Top - FVector(0.0, 0.0, 2.0 * RayReachCm);
		FHitResult Hit;
		if (World->GetWorld()->LineTraceSingleByChannel(Hit, Top, Bottom, ECC_Pawn, Params))
		{
			Probes[I] = {true, terrain::unreal::toCore(Frame,
				TerrainWorldConversions::ToUnrealPoint(Hit.ImpactPoint)).y, Hit.ImpactNormal.Z};
		}
	}
	const terrain::Heightfield& Field = World->State->Session.tiles().heightfield();
	const std::optional<double> Water = terrain::unreal::waterSurfaceAround(Spot.x, Spot.z,
		WaterAroundMeters, [&Field](double X, double Z) { return Field.sampleAt(X, Z); });
	const terrain::unreal::StandCheck Check =
		terrain::unreal::checkStand(Probes, Spot.ground, Water, Limits);
	if (Check.refusal == StandRefusal::None)
	{
		const terrain::unreal::UnrealPoint Ground =
			terrain::unreal::toUnreal(Frame, {Spot.x, Check.altitude, Spot.z});
		OutLocation = FVector(Ground.x, Ground.y, Ground.z + HalfHeight + ClearanceCm);
	}
	return Check.refusal;
}

// The pawn of the game, put on the first spot, watched (TickLanding), its move input held.
void UTerrainDevTools::StartLanding(APawn& Game, TArray<FVector> Spots)
{
	Dev->Landing = FTerrainDevLanding{&Game, MoveTemp(Spots), 0, 0, FPlatformTime::Seconds()};
	if (APlayerController* PlayerController = Controller())
	{
		if (!PlayerController->IsMoveInputIgnored())
		{
			PlayerController->SetIgnoreMoveInput(true);
		}
	}
}

void UTerrainDevTools::TickLanding()
{
	if (!Dev->Landing.has_value())
	{
		return;
	}
	FTerrainDevLanding& Landing = *Dev->Landing;
	APawn* Pawn = Landing.Pawn.Get();
	const double Now = FPlatformTime::Seconds();
	++Landing.Frames;
	const FString Slide = IsValid(Pawn) ? SlideOf(*Pawn, Landing, Now) : FString();
	const bool bWatched = Now - Landing.PlacedAt >= WatchSeconds;
	if (IsValid(Pawn) && Slide.IsEmpty() && !bWatched)
	{
		return;
	}
	if (IsValid(Pawn) && !Slide.IsEmpty() && Landing.Index + 1 < Landing.Spots.Num())
	{
		++Landing.Index;
		ShowAndLog(FString::Printf(TEXT("Terrain.Pawn: the pawn of the game slid (%s): spot %d of "
			"%d"), *Slide, Landing.Index + 1, Landing.Spots.Num()));
		PutDown(*Pawn, Landing.Spots[Landing.Index]);
		Landing.Frames = 0;
		Landing.PlacedAt = Now;
		return;
	}
	if (!Slide.IsEmpty())
	{
		ShowAndLog(FString::Printf(TEXT("Terrain.Pawn: the pawn of the game slid from the %d spots "
			"tried near the view (the last: %s); it stays where it is"), Landing.Spots.Num(),
			*Slide));
	}
	else if (IsValid(Pawn))
	{
		TerrainDev::Log(FString::Printf(TEXT("the pawn of the game stands, spot %d of %d"),
			Landing.Index + 1, Landing.Spots.Num()));
	}
	EndLanding();
}

void UTerrainDevTools::EndLanding()
{
	if (!Dev->Landing.has_value())
	{
		return;
	}
	Dev->Landing.reset();
	if (APlayerController* PlayerController = Controller())
	{
		if (PlayerController->IsMoveInputIgnored())
		{
			PlayerController->SetIgnoreMoveInput(false);
		}
	}
}

#endif // TERRAIN_DEV_TOOLS

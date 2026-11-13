// The aim of a click of the player and of Terrain.EditAim (TerrainBuildAim.h): what was the
// namespace of TerrainBuildComponent.cpp (agent 28), with the point and the radius of a request
// (agent 41). Everything computed is in the Bridge (block_targeting.hpp, build_rules.hpp).
#include "TerrainBuildAim.h"

#include "Bridge/build_rules.hpp"
#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TerrainWorld.h"
#include "TerrainWorldState.h"

namespace TerrainBuildAim
{
	using TerrainWorldConversions::ToUnrealPoint;

	terrain::unreal::BuildTool ToBridge(ETerrainBuildTool Tool)
	{
		return static_cast<terrain::unreal::BuildTool>(static_cast<uint8>(Tool));
	}

	double RadiusOf(const FTerrainBuildRequest& Request)
	{
		return Request.RadiusMeters ? *Request.RadiusMeters
									: terrain::unreal::toolRadius(ToBridge(Request.Tool));
	}

	FTerrainBuildAim AimAt(APawn& Pawn, APlayerController& Controller, ATerrainWorld& World,
		const terrain::unreal::UnrealFrame& Frame, float ReachMeters,
		const FTerrainBuildRequest& Request)
	{
		FTerrainBuildAim Aim;
		Aim.Refusal = FString::Printf(TEXT("no ground within reach (%.1f m of the eye)"),
			ReachMeters);
		if (Controller.PlayerCameraManager == nullptr || Pawn.GetWorld() == nullptr)
		{
			return Aim;
		}
		const FVector Camera = Controller.PlayerCameraManager->GetCameraLocation();
		const FVector Forward = Request.Through
			? (*Request.Through - Camera).GetSafeNormal()
			: Controller.PlayerCameraManager->GetCameraRotation().Vector();
		const FVector Eye = Pawn.GetPawnViewLocation();
		// From the camera (behind the pawn in third person) to the reach of the eye beyond it.
		const double Length = ReachMeters * 100.0 + FVector::Dist(Camera, Eye);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TerrainBuild), true, &Pawn);
		FHitResult Hit;
		if (Forward.IsNearlyZero() ||
			!Pawn.GetWorld()->LineTraceSingleByChannel(Hit, Camera, Camera + Forward * Length,
				ECC_Visibility, Params) ||
			Hit.GetActor() != &World)
		{
			return Aim;
		}
		const terrain::unreal::BuildHit Touched = terrain::unreal::hitFromUnreal(Frame,
			ToUnrealPoint(Hit.ImpactPoint), Hit.ImpactNormal.X, Hit.ImpactNormal.Y,
			Hit.ImpactNormal.Z, FVector::Dist(Hit.ImpactPoint, Eye));
		Aim.Hit = Touched;
		if (Request.Through)
		{
			const terrain::unreal::CorePoint Asked =
				terrain::unreal::toCore(Frame, ToUnrealPoint(*Request.Through));
			if (!terrain::unreal::aimsAt(Touched, Asked))
			{
				const double Away = FVector::Dist(Hit.ImpactPoint, *Request.Through) / 100.0;
				Aim.Refusal = FString::Printf(TEXT("the ray hits %.1f m from the point aimed at"),
					Away);
				return Aim;
			}
		}
		if (!terrain::unreal::withinReach(Touched, ReachMeters))
		{
			return Aim;
		}
		Aim.Target = terrain::unreal::buildTarget(Touched, ToBridge(Request.Tool),
			Request.Action == ETerrainBuildAction::Break ? terrain::unreal::BuildAction::Break
														 : terrain::unreal::BuildAction::Place,
			RadiusOf(Request));
		Aim.Refusal.Reset();
		return Aim;
	}

	bool InCapsule(const APawn& Pawn, const terrain::unreal::UnrealFrame& Frame,
		const terrain::VolumeEdit& Edit)
	{
		const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Pawn.GetRootComponent());
		if (Capsule == nullptr)
		{
			return false;
		}
		const terrain::unreal::CoreCapsule Core = terrain::unreal::capsuleFromUnreal(Frame,
			ToUnrealPoint(Capsule->GetComponentLocation()), Capsule->GetScaledCapsuleHalfHeight(),
			Capsule->GetScaledCapsuleRadius());
		return terrain::unreal::touchesCapsule(Edit, Core);
	}

	FString Describe(const FTerrainBuildRequest& Request, const terrain::VolumeEdit& Edit)
	{
		const std::string Shape = terrain::unreal::shapeName(ToBridge(Request.Tool),
			RadiusOf(Request));
		return FString::Printf(TEXT("%s %s at (%d, %d, %d)"),
			Request.Action == ETerrainBuildAction::Break ? TEXT("break") : TEXT("place"),
			UTF8_TO_TCHAR(Shape.c_str()), Edit.anchor.x, Edit.anchor.y, Edit.anchor.z);
	}
} // namespace TerrainBuildAim

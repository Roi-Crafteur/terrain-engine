#pragma once

// The aim of a click of the player (agent 28) and of Terrain.EditAim (agent 41), one code for
// both (UTerrainBuildComponent::BuildRequested): the trace from the camera on the tiles, the hit
// within reach of the eye of the pawn, the edit of the tool there (Bridge/block_targeting.hpp),
// the capsule of the pawn a placed volume must not close on, the words of the log.

#include "CoreMinimal.h"

#include "Bridge/block_targeting.hpp"
#include "TerrainEdits.h"

#include <optional>

class APawn;
class APlayerController;
class ATerrainWorld;

// What a click asks: the action and the tool of the component; Terrain.EditAim may give the
// radius of a sphere (meters; none: the radius of the tool) and a point the trace goes through
// (Unreal, centimeters; none: the center of the screen, as a click).
struct FTerrainBuildRequest
{
	ETerrainBuildAction Action = ETerrainBuildAction::Break;
	ETerrainBuildTool Tool = ETerrainBuildTool::Block;
	std::optional<double> RadiusMeters;
	std::optional<FVector> Through;
};

// What an aim found: the edit aimed at and its hit, or the refusal; the point the trace touched
// even when refused (out of reach, away from the point asked).
struct FTerrainBuildAim
{
	std::optional<terrain::unreal::BuildTarget> Target;
	std::optional<terrain::unreal::BuildHit> Hit;
	FString Refusal;
};

namespace TerrainBuildAim
{
	terrain::unreal::BuildTool ToBridge(ETerrainBuildTool Tool);

	// The radius of the sphere of a request, meters: the one given, else that of the tool.
	double RadiusOf(const FTerrainBuildRequest& Request);

	// The trace of the camera on the tiles of the world: the first tile hit, within reach of the
	// eye, toward the center of the screen or through the point of the request; with a point,
	// refused when the trace hits more than kAimToleranceMeters from it (an obstacle on the way is
	// never edited).
	FTerrainBuildAim AimAt(APawn& Pawn, APlayerController& Controller, ATerrainWorld& World,
		const terrain::unreal::UnrealFrame& Frame, float ReachMeters,
		const FTerrainBuildRequest& Request);

	// True if a placed volume would close on the capsule of the pawn.
	bool InCapsule(const APawn& Pawn, const terrain::unreal::UnrealFrame& Frame,
		const terrain::VolumeEdit& Edit);

	// "break sphere 3 m at (x, y, z)": the label of the edit on the edit thread.
	FString Describe(const FTerrainBuildRequest& Request, const terrain::VolumeEdit& Edit);
} // namespace TerrainBuildAim

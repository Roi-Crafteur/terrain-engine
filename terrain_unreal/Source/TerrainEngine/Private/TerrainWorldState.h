#pragma once

#include "CoreMinimal.h"

#include "Bridge/horizon_gpu_streamer.hpp"
#include "Bridge/horizon_streamer.hpp"
#include "Bridge/horizon_timings.hpp"
#include "Bridge/terrain_session.hpp"
#include "Bridge/unreal_frame.hpp"
#include "TerrainWorldLoading.h"

#include <memory>
#include <string>

// The terrain_core side of a playing ATerrainWorld: its session (tiles, timings, streamer), the
// frame placing the core world in the Unreal world, and the far horizon.
struct FTerrainWorldState
{
	explicit FTerrainWorldState(const terrain::unreal::SessionParams& Params)
		: Session(Params)
	{
	}

	terrain::unreal::TerrainSession Session;
	// Fixed while playing, rebasing aside: the player keeps unwrapped coordinates across the edges
	// of the wrapped world, the tiles follow it to their nearest images (Bridge/tile_streamer.hpp).
	terrain::unreal::UnrealFrame Frame;
	// After the session: their workers and jobs read the relief of the tiles, and stop first. One
	// of them, as ATerrainWorld::HorizonRenderer chose.
	std::unique_ptr<terrain::unreal::HorizonMeshStreamer> Horizon;
	std::unique_ptr<terrain::unreal::HorizonGpuStreamer> HorizonGpu;
	terrain::unreal::HorizonTimings HorizonTimings;
	double HorizonStartSeconds = 0.0;
	// The frames since the last summary: game thread, render thread and GPU (as stat unit).
	terrain::unreal::StageStats GameThreadFrames;
	terrain::unreal::StageStats RenderThreadFrames;
	terrain::unreal::StageStats GpuFrames;
	// The measures of the loads at the spawn and after the teleports (TerrainWorldLoading.cpp).
	FTerrainLoadingState Loading;
};

namespace TerrainWorldConversions
{
	inline terrain::unreal::UnrealPoint ToUnrealPoint(const FVector& Vector)
	{
		return {Vector.X, Vector.Y, Vector.Z};
	}

	inline FString ToFString(const std::string& Text)
	{
		return FString(UTF8_TO_TCHAR(Text.c_str()));
	}
} // namespace TerrainWorldConversions

// The far horizon of ATerrainWorld (phase 13): its start, its tick and its measures.
#include "TerrainWorld.h"

#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "TerrainEngineLog.h"
#include "TerrainHorizonMeshes.h"
#include "TerrainWorldState.h"

#include "terrain/streaming/horizon_levels.hpp"

#include <exception>
#include <memory>
#include <string>

namespace
{
	using TerrainWorldConversions::ToFString;
	using TerrainWorldConversions::ToUnrealPoint;

	// Created by terrain_unreal/Scripts/create_horizon_material.py.
	const TCHAR* const GeneratedHorizonMaterialPath =
		TEXT("/Game/TerrainEngine/M_TerrainHorizon.M_TerrainHorizon");

	constexpr double MegaBytes = 1024.0 * 1024.0;

	terrain::unreal::HorizonMeshParams MakeHorizonParams(const ATerrainWorld& World,
		const terrain::LodParams& Lod)
	{
		terrain::unreal::HorizonMeshParams Params;
		Params.levels = FMath::Clamp(World.HorizonLevels, 1, 10);
		Params.workerThreads = FMath::Max(1, World.HorizonThreads);
		Params.look.dropMeters = FMath::Max(0.0f, World.HorizonDropCm) / 100.0;
		Params.look.sea = World.bHorizonSea;
		Params.planetRadiusMeters = FMath::Max(0.0, World.PlanetRadiusKm) * 1000.0;
		Params.lod = Lod;
		return Params;
	}

	bool IsCurved(const UMaterialInterface* Material)
	{
		float Radius = 0.0f;
		return Material != nullptr &&
			Material->GetScalarParameterValue(
				FHashedMaterialParameterInfo(UTerrainHorizonMeshes::PlanetRadiusParameter), Radius);
	}
} // namespace

void ATerrainWorld::StartHorizon()
{
	if (!bHorizon)
	{
		return;
	}
	terrain::unreal::TerrainSession& Session = State->Session;
	const terrain::StreamParams& Stream = Session.streamer().manager().params();
	try
	{
		State->Horizon = std::make_unique<terrain::unreal::HorizonMeshStreamer>(
			Session.tiles().heightfield(), Session.tiles().ecology(), Stream.world,
			MakeHorizonParams(*this, Stream.lod));
	}
	catch (const std::exception& Exception)
	{
		State->Horizon.reset();
		UE_LOG(LogTerrainEngine, Error, TEXT("Terrain World: no far horizon: %s"),
			*ToFString(Exception.what()));
		return;
	}
	const terrain::unreal::HorizonMeshStreamer& Horizon = *State->Horizon;
	UMaterialInterface* HorizonLook = ResolveHorizonMaterial();
	HorizonMeshes = NewObject<UTerrainHorizonMeshes>(this);
	HorizonMeshes->Initialize(*this, HorizonLook, Horizon.planetRadiusMeters() * 100.0,
		bHorizonShadows);
	State->HorizonStartSeconds = FPlatformTime::Seconds();
	const terrain::unreal::TileHole& Hole = Horizon.tileHole();
	UE_LOG(LogTerrainEngine, Log,
		TEXT("Terrain World: far horizon of %d levels up to %.0f km, %s by a planet of %.0f km, "
			 "hole of %d m under the tiles with %.0f m of tiles over level 0, %d threads"),
		Horizon.params().levels, terrain::guaranteedRadius(Horizon.params()) / 1000.0,
		IsCurved(HorizonLook) ? TEXT("curved") : TEXT("NOT curved (no PlanetRadiusCm)"),
		Horizon.planetRadiusMeters() / 1000.0,
		Hole.halfCells * terrain::levelPitch(Horizon.params(), 0), Hole.overlapMeters,
		HorizonThreads);
}

void ATerrainWorld::StopHorizon(const FString& Reason)
{
	UE_LOG(LogTerrainEngine, Error, TEXT("Terrain World: far horizon stopped: %s"), *Reason);
	if (HorizonMeshes != nullptr)
	{
		HorizonMeshes->RemoveAll();
		HorizonMeshes = nullptr;
	}
	State->Horizon.reset();
}

// The update of the horizon, all within this frame: the levels of an epoch move together.
void ATerrainWorld::TickHorizon(const FVector& Focus)
{
	if (State->Horizon == nullptr || HorizonMeshes == nullptr)
	{
		return;
	}
	const double Start = FPlatformTime::Seconds();
	try
	{
		const terrain::unreal::HorizonUpdate Update =
			State->Horizon->update(ToUnrealPoint(Focus), State->Frame);
		HorizonMeshes->Apply(Update, State->HorizonTimings);
		const double Milliseconds = (FPlatformTime::Seconds() - Start) * 1000.0;
		State->HorizonTimings.recordTick(Update.coarsestMoved, Update.first, Milliseconds);
		if (Update.committed)
		{
			State->HorizonTimings.recordEpoch(Update.epochMilliseconds, Update.samples);
		}
		if (!Update.moves.empty())
		{
			UE_LOG(LogTerrainEngine, Log,
				TEXT("Terrain World: %d far horizon levels moved to other images (the first one to "
					 "(%.0f, %.0f) km), in %.2f ms"),
				static_cast<int32>(Update.moves.size()), Update.moves.front().origin.x / 1.0e5,
				Update.moves.front().origin.y / 1.0e5, Milliseconds);
		}
		if (Update.first)
		{
			UE_LOG(LogTerrainEngine, Log,
				TEXT("Terrain World: far horizon shown %.2f s after its start (%lld samples), in "
					 "%.1f ms on the game thread: %lld vertices, %lld triangles"),
				Start - State->HorizonStartSeconds, static_cast<long long>(Update.samples),
				Milliseconds, HorizonMeshes->GetVertexCount(), HorizonMeshes->GetTriangleCount());
		}
	}
	catch (const std::exception& Exception)
	{
		StopHorizon(ToFString(Exception.what()));
	}
}

void ATerrainWorld::LogHorizon()
{
	if (State->Horizon == nullptr || HorizonMeshes == nullptr)
	{
		return;
	}
	for (const std::string& Line : terrain::unreal::describe(State->HorizonTimings.take()))
	{
		UE_LOG(LogTerrainEngine, Log, TEXT("  %s"), *ToFString(Line));
	}
	UE_LOG(LogTerrainEngine, Log,
		TEXT("  horizon memory: %.1f MB of samples and topologies, %lld vertices and %lld "
			 "triangles in the meshes, %.1f MB kept by the procedural meshes"),
		static_cast<double>(State->Horizon->memoryBytes()) / MegaBytes,
		HorizonMeshes->GetVertexCount(), HorizonMeshes->GetTriangleCount(),
		static_cast<double>(HorizonMeshes->GetMeshBytes()) / MegaBytes);
}

UMaterialInterface* ATerrainWorld::ResolveHorizonMaterial() const
{
	if (HorizonMaterial != nullptr)
	{
		return HorizonMaterial;
	}
	if (UMaterialInterface* Generated = LoadObject<UMaterialInterface>(
			nullptr, GeneratedHorizonMaterialPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		return Generated;
	}
	UE_LOG(LogTerrainEngine, Warning,
		TEXT("Terrain World: no horizon material set and no %s (terrain_unreal/Scripts/"
			 "create_horizon_material.py creates it): the horizon takes the material of the tiles "
			 "and stays flat"),
		GeneratedHorizonMaterialPath);
	return ResolveMaterial();
}

// The far horizon of ATerrainWorld (phase 13): its start, its tick and its measures, drawn by the
// GPU (textures and fixed grids, step 2) or by the CPU (procedural meshes, step 1).
#include "TerrainWorld.h"

#include "Bridge/horizon_atmosphere.hpp"
#include "DynamicRHI.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "RenderTimer.h"
#include "Tasks/Task.h"
#include "TerrainAtmosphereFollower.h"
#include "TerrainEngineLog.h"
#include "TerrainHorizonClipmap.h"
#include "TerrainHorizonMeshes.h"
#include "TerrainWorldState.h"

#include "terrain/streaming/horizon_levels.hpp"

#include <algorithm>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace
{
	using TerrainWorldConversions::ToFString;
	using TerrainWorldConversions::ToUnrealPoint;

	// Created by terrain_unreal/Scripts/create_horizon_material.py and
	// create_horizon_gpu_material.py.
	const TCHAR* const GeneratedHorizonMaterialPath =
		TEXT("/Game/TerrainEngine/M_TerrainHorizon.M_TerrainHorizon");
	const TCHAR* const GeneratedHorizonGpuMaterialPath =
		TEXT("/Game/TerrainEngine/M_TerrainHorizonGpu.M_TerrainHorizonGpu");

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
		Params.crestRatio = terrain::unreal::crestRatioSetting(World.HorizonCrestRatio);
		return Params;
	}

	terrain::unreal::HorizonGpuParams MakeHorizonGpuParams(const ATerrainWorld& World,
		const terrain::LodParams& Lod)
	{
		terrain::unreal::HorizonGpuParams Params;
		Params.levels = FMath::Clamp(World.HorizonLevels, 1, 10);
		Params.maxPiecesInFlight = FMath::Max(1, World.HorizonThreads);
		Params.normalRatio = World.bHorizonDetailNormals
			? static_cast<int>(FMath::RoundUpToPowerOfTwo(
				static_cast<uint32>(FMath::Clamp(World.HorizonNormalRatio, 2, 8))))
			: 0;
		Params.normalJobs = FMath::Max(1, World.HorizonNormalJobs);
		Params.crestRatio = terrain::unreal::crestRatioSetting(World.HorizonCrestRatio);
		Params.sea = World.bHorizonSea;
		Params.planetRadiusMeters = FMath::Max(0.0, World.PlanetRadiusKm) * 1000.0;
		Params.lod = Lod;
		return Params;
	}

	// The pieces of the GPU horizon run as low priority tasks: the workers of the tiles, threads
	// of normal priority, go first.
	terrain::unreal::JobLauncher LowPriorityTasks()
	{
		return [](std::function<void()> Job)
		{
			UE::Tasks::Launch(TEXT("TerrainHorizonPiece"), [Job = std::move(Job)] { Job(); },
				UE::Tasks::ETaskPriority::BackgroundLow);
		};
	}

	bool IsCurved(const UMaterialInterface* Material)
	{
		float Radius = 0.0f;
		return Material != nullptr &&
			Material->GetScalarParameterValue(
				FHashedMaterialParameterInfo(UTerrainHorizonMeshes::PlanetRadiusParameter), Radius);
	}

	void AddFrame(terrain::unreal::StageStats& Stats, uint32 Cycles)
	{
		const double Milliseconds = FPlatformTime::ToMilliseconds(Cycles);
		++Stats.tiles;
		Stats.totalMilliseconds += Milliseconds;
		Stats.maxMilliseconds = std::max(Stats.maxMilliseconds, Milliseconds);
	}
} // namespace

void ATerrainWorld::StartHorizon()
{
	if (!bHorizon)
	{
		return;
	}
	if (HorizonRenderer == ETerrainHorizonRenderer::Gpu && StartGpuHorizon())
	{
		return;
	}
	StartCpuHorizon();
}

// False without the material of the GPU horizon: the CPU one is drawn instead.
bool ATerrainWorld::StartGpuHorizon()
{
	UMaterialInterface* GpuLook = ResolveHorizonGpuMaterial();
	if (GpuLook == nullptr)
	{
		return false;
	}
	terrain::unreal::TerrainSession& Session = State->Session;
	const terrain::StreamParams& Stream = Session.streamer().manager().params();
	try
	{
		State->HorizonGpu = std::make_unique<terrain::unreal::HorizonGpuStreamer>(
			Session.tiles().heightfield(), Session.tiles().ecology(), Stream.world,
			MakeHorizonGpuParams(*this, Stream.lod), LowPriorityTasks());
	}
	catch (const std::exception& Exception)
	{
		State->HorizonGpu.reset();
		UE_LOG(LogTerrainEngine, Error, TEXT("Terrain World: no far horizon: %s"),
			*ToFString(Exception.what()));
		return true;
	}
	const terrain::unreal::HorizonGpuStreamer& Horizon = *State->HorizonGpu;
	FTerrainHorizonLook Look;
	Look.DropCm = FMath::Max(0.0f, HorizonDropCm);
	Look.MorphCells = FMath::Clamp(HorizonMorphCells, 1.0f, 100.0f);
	Look.bShadows = bHorizonShadows;
	Look.bDebugTint = bHorizonDebugTint;
	HorizonClipmap = NewObject<UTerrainHorizonClipmap>(this);
	HorizonClipmap->Initialize(*this, GpuLook, Horizon, Look);
	HorizonClipmap->BindSurfaceLook(TileSurfaceLook);
	HorizonClipmap->SetWaveOrigin(terrain::unreal::seaWaveOrigin(State->Frame));
	State->HorizonStartSeconds = FPlatformTime::Seconds();
	const terrain::unreal::TileHole& Hole = Horizon.tileHole();
	UE_LOG(LogTerrainEngine, Log,
		TEXT("Terrain World: GPU far horizon of %d levels up to %.0f km, planet of %.0f km, hole of "
			 "%d m under the tiles with %.0f m of tiles over level 0, %d pieces at once as low "
			 "priority tasks, detail normals %s, grids built in %.1f ms, %.1f MB on the GPU"),
		Horizon.params().levels, terrain::guaranteedRadius(Horizon.params()) / 1000.0,
		Horizon.planetRadiusMeters() / 1000.0,
		Hole.halfCells * terrain::levelPitch(Horizon.params(), 0), Hole.overlapMeters,
		HorizonThreads,
		Horizon.normals() != nullptr
			? *FString::Printf(TEXT("%d x finer by %d tasks (%.1f MB)"), Horizon.normals()->ratio(),
				FMath::Max(1, HorizonNormalJobs),
				static_cast<double>(Horizon.normals()->textureBytes()) / MegaBytes)
			: TEXT("off"),
		HorizonClipmap->GetBuildMilliseconds(),
		static_cast<double>(HorizonClipmap->GetGpuBytes()) / MegaBytes);
	return true;
}

void ATerrainWorld::StartCpuHorizon()
{
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
		TEXT("Terrain World: CPU far horizon of %d levels up to %.0f km, %s by a planet of %.0f km, "
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
	if (HorizonClipmap != nullptr)
	{
		HorizonClipmap->RemoveAll();
		HorizonClipmap = nullptr;
	}
	State->Horizon.reset();
	State->HorizonGpu.reset();
}

// The update of the horizon, all within this frame: the levels of an epoch move together.
void ATerrainWorld::TickHorizon(const FVector& Focus)
{
	const bool bGpu = State->HorizonGpu != nullptr && HorizonClipmap != nullptr;
	if (!bGpu && (State->Horizon == nullptr || HorizonMeshes == nullptr))
	{
		return;
	}
	const double Start = FPlatformTime::Seconds();
	try
	{
		int32 CoarsestMoved = -1;
		bool bFirst = false;
		bool bCommitted = false;
		double EpochMilliseconds = 0.0;
		std::int64_t Samples = 0;
		std::size_t Moves = 0;
		if (bGpu)
		{
			terrain::unreal::HorizonGpuUpdate Update =
				State->HorizonGpu->update(ToUnrealPoint(Focus), State->Frame);
			HorizonClipmap->Apply(Update, State->HorizonTimings);
			CoarsestMoved = Update.coarsestMoved;
			bFirst = Update.first;
			bCommitted = Update.committed;
			EpochMilliseconds = Update.epochMilliseconds;
			Samples = Update.samples;
			Moves = Update.committed ? 0 : Update.placements.size();
		}
		else
		{
			const terrain::unreal::HorizonUpdate Update =
				State->Horizon->update(ToUnrealPoint(Focus), State->Frame);
			HorizonMeshes->Apply(Update, State->HorizonTimings);
			CoarsestMoved = Update.coarsestMoved;
			bFirst = Update.first;
			bCommitted = Update.committed;
			EpochMilliseconds = Update.epochMilliseconds;
			Samples = Update.samples;
			Moves = Update.moves.size();
		}
		const double Milliseconds = (FPlatformTime::Seconds() - Start) * 1000.0;
		State->HorizonTimings.recordTick(CoarsestMoved, bFirst, Milliseconds);
		if (bCommitted)
		{
			State->HorizonTimings.recordEpoch(EpochMilliseconds, Samples);
		}
		if (Moves > 0)
		{
			UE_LOG(LogTerrainEngine, Log,
				TEXT("Terrain World: %d far horizon levels placed again without an epoch (a world "
					 "turn, the origin rebasing), in %.2f ms"),
				static_cast<int32>(Moves), Milliseconds);
		}
		if (bFirst)
		{
			UE_LOG(LogTerrainEngine, Log,
				TEXT("Terrain World: far horizon shown %.2f s after its start (%lld samples), in "
					 "%.1f ms on the game thread"),
				Start - State->HorizonStartSeconds, static_cast<long long>(Samples), Milliseconds);
		}
	}
	catch (const std::exception& Exception)
	{
		StopHorizon(ToFString(Exception.what()));
	}
}

// The times of the last frame as stat unit shows them.
void ATerrainWorld::RecordFrame()
{
	AddFrame(State->GameThreadFrames, GGameThreadTime);
	AddFrame(State->RenderThreadFrames, GRenderThreadTime);
	AddFrame(State->GpuFrames, RHIGetGPUFrameCycles(0));
}

void ATerrainWorld::LogHorizon()
{
	const terrain::unreal::StageStats Game = std::exchange(State->GameThreadFrames, {});
	const terrain::unreal::StageStats Render = std::exchange(State->RenderThreadFrames, {});
	const terrain::unreal::StageStats Gpu = std::exchange(State->GpuFrames, {});
	UE_LOG(LogTerrainEngine, Log,
		TEXT("  frames: %d, game thread %.2f ms (max %.2f), render thread %.2f ms (max %.2f), GPU "
			 "%.2f ms (max %.2f)"),
		static_cast<int32>(Game.tiles), Game.meanMilliseconds(), Game.maxMilliseconds,
		Render.meanMilliseconds(), Render.maxMilliseconds, Gpu.meanMilliseconds(),
		Gpu.maxMilliseconds);
	if (AtmosphereFollower != nullptr)
	{
		const terrain::unreal::StageStats Sky = AtmosphereFollower->TakeTimings();
		UE_LOG(LogTerrainEngine, Log, TEXT("  sky following the camera: %d ticks %.4f ms (max %.4f)"),
			static_cast<int32>(Sky.tiles), Sky.meanMilliseconds(), Sky.maxMilliseconds);
	}
	if (State->Horizon == nullptr && State->HorizonGpu == nullptr)
	{
		return;
	}
	for (const std::string& Line : terrain::unreal::describe(State->HorizonTimings.take()))
	{
		UE_LOG(LogTerrainEngine, Log, TEXT("  %s"), *ToFString(Line));
	}
	if (State->HorizonGpu != nullptr && HorizonClipmap != nullptr)
	{
		UE_LOG(LogTerrainEngine, Log,
			TEXT("  horizon memory: %.1f MB of samples, %lld vertices and %lld triangles drawn, "
				 "%.1f MB of textures and grids on the GPU, no mesh kept on the CPU"),
			static_cast<double>(State->HorizonGpu->memoryBytes()) / MegaBytes,
			HorizonClipmap->GetVertexCount(), HorizonClipmap->GetTriangleCount(),
			static_cast<double>(HorizonClipmap->GetGpuBytes()) / MegaBytes);
	}
	else if (State->Horizon != nullptr && HorizonMeshes != nullptr)
	{
		UE_LOG(LogTerrainEngine, Log,
			TEXT("  horizon memory: %.1f MB of samples and topologies, %lld vertices and %lld "
				 "triangles in the meshes, %.1f MB kept by the procedural meshes"),
			static_cast<double>(State->Horizon->memoryBytes()) / MegaBytes,
			HorizonMeshes->GetVertexCount(), HorizonMeshes->GetTriangleCount(),
			static_cast<double>(HorizonMeshes->GetMeshBytes()) / MegaBytes);
	}
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

UMaterialInterface* ATerrainWorld::ResolveHorizonGpuMaterial() const
{
	if (HorizonGpuMaterial != nullptr)
	{
		return HorizonGpuMaterial;
	}
	if (UMaterialInterface* Generated = LoadObject<UMaterialInterface>(
			nullptr, GeneratedHorizonGpuMaterialPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		return Generated;
	}
	UE_LOG(LogTerrainEngine, Warning,
		TEXT("Terrain World: no GPU horizon material set and no %s (terrain_unreal/Scripts/"
			 "create_horizon_gpu_material.py creates it): the CPU horizon is drawn instead"),
		GeneratedHorizonGpuMaterialPath);
	return nullptr;
}

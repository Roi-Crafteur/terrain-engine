// The uploads of the tiles to the meshes within the time of a frame (agent 38): the backend as the
// sink of Bridge/upload_pacer.hpp, each upload timed and recorded.
#include "TerrainWorldUploads.h"

#include "Bridge/load_timeline.hpp"
#include "Bridge/tile_timings.hpp"
#include "HAL/PlatformTime.h"
#include "TerrainEngineLog.h"
#include "TerrainMeshBackend.h"
#include "TerrainWorldLoading.h"
#include "TerrainWorldState.h"

#include <utility>

namespace
{
	using TerrainWorldConversions::ToUnrealPoint;

	// The meshes of Unreal under the uploads, the moves and the removals of the updates.
	class FTerrainBackendSink final : public terrain::unreal::UploadSink
	{
	public:
		FTerrainBackendSink(UTerrainMeshBackend& InBackend, FTerrainWorldState& InState)
			: Backend(InBackend), Session(InState.Session), State(InState)
		{
		}

		void upload(const terrain::unreal::TileUpload& Upload) override
		{
			const double Start = FPlatformTime::Seconds();
			Backend.SetTile(Upload);
			const double UploadMs = (FPlatformTime::Seconds() - Start) * 1000.0;
			Session.timings().record(terrain::unreal::TileStage::Upload, Upload.key.lod, UploadMs);
			Session.timeline().recordShown(Upload.key, terrain::unreal::LoadClock::now(), UploadMs,
				Upload.collision);
			UE_LOG(LogTerrainEngine, Verbose,
				TEXT("Tile (%d, %d) LOD %d: %d vertices, %d triangles, collision %s, conversion "
					 "%.2f ms, upload %.2f ms"),
				Upload.key.origin.x, Upload.key.origin.z, Upload.key.lod,
				static_cast<int32>(Upload.mesh.positions.size()),
				static_cast<int32>(Upload.mesh.triangleCount()),
				Upload.collision ? TEXT("yes") : TEXT("no"), Upload.conversionMilliseconds,
				UploadMs);
#if TERRAIN_DEV_TOOLS
			// The edits of the players this tile shows: the time from their click (agent 28).
			if (State.Build.Latency.uploaded(Upload.key, Session.tiles().world(),
					FPlatformTime::Seconds(), static_cast<int64>(GFrameCounter)) > 0)
			{
				const terrain::unreal::EditTiming& Last = State.Build.Latency.timings().back();
				UE_LOG(LogTerrainEngine, Log, TEXT("Terrain World: edit shown %.1f ms after its "
					"click (%.1f ms to be done), %lld frames later"), Last.shownSeconds * 1000.0,
					Last.doneSeconds * 1000.0, static_cast<long long>(Last.frames));
			}
#endif
		}

		void move(const terrain::unreal::TileMove& Move) override
		{
			Backend.MoveTile(Move.key, Move.origin);
		}

		void remove(const terrain::TileKey& Key) override
		{
			Backend.RemoveTile(Key);
		}

	private:
		UTerrainMeshBackend& Backend;
		terrain::unreal::TerrainSession& Session;
		FTerrainWorldState& State;
	};

	// The update handed to the pacer, its tiles marked converted (they left the streamer).
	void StartTerrainUpdate(FTerrainWorldState& State, terrain::unreal::StreamerUpdate Update)
	{
		const terrain::unreal::LoadClock::time_point Converted = terrain::unreal::LoadClock::now();
		for (const terrain::unreal::TileUpload& Upload : Update.uploads)
		{
			State.Session.timeline().recordConverted(Upload.key, Converted);
		}
		State.Loading.Pacer.start(std::move(Update));
	}
} // namespace

FTerrainUploads TickTerrainUploads(UTerrainMeshBackend& Backend, FTerrainWorldState& State,
	const FVector& Focus)
{
	FTerrainLoadingState& Loading = State.Loading;
	Loading.FrameTilesMilliseconds = 0.0;
	if (!Loading.Pacer.busy())
	{
		const double Start = FPlatformTime::Seconds();
		StartTerrainUpdate(State, State.Session.streamer().update(ToUnrealPoint(Focus), State.Frame));
		Loading.FrameTilesMilliseconds = (FPlatformTime::Seconds() - Start) * 1000.0;
	}
	terrain::unreal::UploadCostModel& Costs = Loading.UploadCosts;
	terrain::unreal::UploadBudget Budget;
	Budget.milliseconds = Loading.Applied.uploadMilliseconds;
	Budget.estimate = [&Costs](const terrain::unreal::TileUpload& Upload)
	{ return Costs.estimate(Upload.mesh.positions.size(), Upload.collision); };
	Budget.measured = [&Costs](const terrain::unreal::TileUpload& Upload, double Milliseconds)
	{ Costs.record(Upload.mesh.positions.size(), Upload.collision, Milliseconds); };
	FTerrainBackendSink Sink(Backend, State);
	const terrain::unreal::PacedFrame Frame = Loading.Pacer.apply(Sink, Budget);
	FTerrainUploads Uploads;
	Uploads.Count = static_cast<int32>(Frame.uploads);
	Uploads.Milliseconds = Frame.milliseconds;
	return Uploads;
}

void FlushTerrainUploads(UTerrainMeshBackend& Backend, FTerrainWorldState& State)
{
	FTerrainBackendSink Sink(Backend, State);
	static_cast<void>(State.Loading.Pacer.flush(Sink));
}

void ApplyTerrainUpdateNow(UTerrainMeshBackend& Backend, FTerrainWorldState& State,
	const terrain::unreal::StreamerUpdate& Update)
{
	FlushTerrainUploads(Backend, State);
	StartTerrainUpdate(State, Update);
	FlushTerrainUploads(Backend, State);
}

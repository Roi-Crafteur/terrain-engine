#pragma once

#include "horizon_layout.hpp"
#include "horizon_mesh.hpp"
#include "horizon_timings.hpp"
#include "tile_mesh_conversion.hpp"
#include "unreal_frame.hpp"

#include "terrain/generation/ecology.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/horizon_sampler.hpp"
#include "terrain/streaming/horizon_streamer.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/world_size.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace terrain::unreal {

struct HorizonMeshParams {
    int levels = 10;
    int cellsPerSide = 256; // K, as HorizonParams
    // Threads computing the samples, besides the tiles' workers. 0: no worker, the samples are
    // only computed by waitIdle (tests).
    int workerThreads = 2;
    HorizonLook look;
    double planetRadiusMeters = 0.0; // 0: planetRadiusMeters(world)
    LodParams lod;                   // the tiles: level 0 leaves out the area under them
    // The crests of the far mountains (HorizonParams::crestRatio): 0 for point samples.
    int crestRatio = HorizonParams{}.crestRatio;
};

// A section of the mesh of a level to show, the mesh placed at origin: the low corner of the
// window of the level at its image nearest to the player (levelOrigin). The triangles of a body
// never change: they come with its first upload only (newTriangles); later uploads of the body
// replace its vertices. A trim always comes with its triangles.
struct HorizonUpload {
    int level = 0;
    HorizonSection section = HorizonSection::Body;
    UnrealPoint origin;
    UnrealMesh mesh;
    bool newTriangles = true;
    double conversionMilliseconds = 0.0;
};

// A level shown whose nearest image changed (a world turn, the origin rebasing of Unreal): both
// its sections move to this origin, by whole worlds or by the offset, unchanged otherwise.
struct HorizonMove {
    int level = 0;
    UnrealPoint origin;
};

// What the meshes of Unreal must do after one update, all within one frame: the levels of an
// epoch move together, so the levels shown always nest without gap or overlap.
struct HorizonUpdate {
    std::vector<HorizonUpload> uploads;
    std::vector<HorizonMove> moves;
    // With an epoch committed: the coarsest level it moved (-1: none), whether it showed the
    // levels for the first time, the time from its plan to its commit and the samples it wrote.
    bool committed = false;
    int coarsestMoved = -1;
    bool first = false;
    double epochMilliseconds = 0.0;
    std::int64_t samples = 0;

    [[nodiscard]] bool empty() const { return uploads.empty() && moves.empty(); }
};

// The far horizon of an Unreal level (phase 13): the HorizonStreamer of terrain_core around the
// player, whose levels become meshes (horizon_layout.hpp, horizon_mesh.hpp) as soon as an epoch
// is committed (the levels are only valid until the next update). Every method must be called
// from the thread owning it; the workers of the HorizonStreamer compute the samples.
//
// As the tiles (TileStreamer), the player keeps its unwrapped position: each level is shown at
// its image nearest to it, and only moves when that image changes.
class HorizonMeshStreamer {
public:
    // field and ecology (those of the tiles) and the edits (TerrainSession::horizonEdits, whose
    // staged edits the epochs apply between them) must outlive the streamer and share the world.
    // Throws std::invalid_argument for invalid parameters, or tiles that do not end inside level
    // 0 (tileHole).
    HorizonMeshStreamer(const Heightfield& field, const Ecology& ecology, const WorldSize& world,
                        const HorizonMeshParams& params, HorizonEditMap* edits = nullptr);

    HorizonMeshStreamer(const HorizonMeshStreamer&) = delete;
    HorizonMeshStreamer& operator=(const HorizonMeshStreamer&) = delete;

    // One update around the player: commits the epoch in flight once computed and plans the next
    // one (HorizonStreamer::update), converts the sections it changed, moves the levels whose
    // image changed. Rethrows the errors of the workers.
    HorizonUpdate update(const UnrealPoint& player, const UnrealFrame& frame);

    // Waits until the epoch in flight is computed (computes it without workers).
    void waitIdle() { streamer_.waitIdle(); }

    [[nodiscard]] const HorizonParams& params() const { return horizonParams_; }
    [[nodiscard]] const TileHole& tileHole() const { return hole_; }
    [[nodiscard]] double planetRadiusMeters() const { return radius_; }
    [[nodiscard]] const terrain::HorizonStreamer& streamer() const { return streamer_; }
    // Levels shown: their first epoch is committed.
    [[nodiscard]] bool shown() const { return streamer_.clipmap().placed(); }
    // Bytes of the samples of the levels, and of the topologies kept for their bodies.
    [[nodiscard]] std::size_t memoryBytes() const;

private:
    [[nodiscard]] BodyShape shapeOf(int level) const;
    [[nodiscard]] HorizonUpload body(int level, const UnrealPoint& origin);
    [[nodiscard]] HorizonUpload trim(int level, const UnrealPoint& origin) const;
    [[nodiscard]] bool writesUnderFiner(const HorizonLevelChange& change) const;
    void convertChanges(const HorizonChanges& changes, const CorePoint& observer,
                        const UnrealFrame& frame, HorizonUpdate& result);
    void placeLevels(const CorePoint& observer, const UnrealFrame& frame, HorizonUpdate& result);

    HorizonParams horizonParams_;
    HorizonLook look_;
    TileHole hole_;
    double radius_;
    HorizonSampler sampler_;
    std::vector<BodyTopology> topologies_;
    std::vector<bool> bodyShown_;
    std::vector<std::optional<UnrealPoint>> origins_; // per level, once shown
    std::optional<std::chrono::steady_clock::time_point> epochStart_;
    terrain::HorizonStreamer streamer_; // last: its workers stop before the sampler goes away
};

} // namespace terrain::unreal

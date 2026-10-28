#pragma once

#include "horizon_epochs.hpp"
#include "horizon_layout.hpp"
#include "horizon_normal_stream.hpp"
#include "horizon_textures.hpp"
#include "unreal_frame.hpp"

#include "terrain/generation/ecology.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/horizon_levels.hpp"
#include "terrain/streaming/horizon_sampler.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/world_size.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace terrain::unreal {

struct HorizonGpuParams {
    int levels = 10;
    int cellsPerSide = 256; // K, as HorizonParams
    int maxPiecesInFlight = 2;
    bool sea = true;                 // the ocean flat at the sea level, a sea square under the tiles
    double planetRadiusMeters = 0.0; // 0: planetRadiusMeters(world)
    LodParams lod;                   // the tiles: level 0 leaves out the area under them
    // The detail normals (HorizonNormalStream): detail texels per sample along each axis, 0 for
    // none (the normals of the grids only, as before them), and the jobs computing them at once.
    int normalRatio = 4;
    int normalJobs = 2;
    // The crests of the far mountains (HorizonParams::crestRatio): 0 for point samples, the
    // horizon before them.
    int crestRatio = HorizonParams{}.crestRatio;
    // The crests computed after the epochs, with the detail normals, from the fine grid they read
    // (HorizonDetailSampler): the levels show as soon as their point samples are ready, the epochs
    // cost what they cost without crests, and the texels of the samples that rise are written
    // again when their crests arrive (horizon_crest_texels.hpp), the same bits as crests computed
    // in the epochs wherever no player edit lies. A sample with an edit keeps the height of its
    // epoch, without crest. False: the crests in the epochs (the horizon of #41).
    bool deferCrests = true;
};

// Where a level of the GPU horizon stands and how its material reads its textures.
struct LevelPlacement {
    int level = 0;
    UnrealPoint origin;     // low corner of the window, at its image nearest to the player
    HorizonIndex firstSlot; // texel of the sample at the low corner of the window
    int trim = -1;          // its trim (trimVariant), -1 for level 0
    // Low corner of the window in the window of the next coarser level, in its cells, for the
    // morph of the normals toward its texels; (-1, -1) for the coarsest level.
    HorizonIndex inCoarser{-1, -1};

    friend bool operator==(const LevelPlacement&, const LevelPlacement&) = default;
};

// What the GPU horizon must do within one frame: write the texels, then place the levels. The
// levels of an epoch move together, so they always nest without gap or overlap.
struct HorizonGpuUpdate {
    std::vector<TexelRegion> regions;
    std::vector<NormalRegion> normalRegions; // detail normals: pending, then computed
    std::vector<LevelPlacement> placements;  // the levels whose placement changed
    // With an epoch committed: the coarsest level it moved (-1: none), whether it showed the
    // levels for the first time, the time from its plan to its commit, the samples it wrote.
    bool committed = false;
    int coarsestMoved = -1;
    bool first = false;
    double epochMilliseconds = 0.0;
    std::int64_t samples = 0;
    // On the update where the detail (crests and normals) of every committed epoch is in: the
    // time since the commit of the oldest of them; 0 otherwise.
    double detailMilliseconds = 0.0;

    [[nodiscard]] bool empty() const {
        return regions.empty() && normalRegions.empty() && placements.empty();
    }
};

// The far horizon drawn by the GPU (phase 13, step 2): the epochs of the clipmap of terrain_core
// run as jobs (HorizonEpochRunner), their texels prepared on the jobs; each commit writes the
// texels of the samples entering the windows, and the levels are placed again whenever their
// window, their trim or their image nearest to the player changes (a world turn, the origin
// rebasing). The detail (normals, and the crests when deferred) follows each commit as jobs of a
// HorizonNormalStream. The fixed grids, the textures and the material belong to the renderer.
// Every method must be called from the thread owning the streamer.
class HorizonGpuStreamer {
public:
    // field and ecology (those of the tiles) must outlive the streamer and share the world; edits
    // summarize the player edits for the epochs (HorizonSampler), none by default. Throws
    // std::invalid_argument for invalid parameters, or tiles that do not end inside level 0.
    HorizonGpuStreamer(const Heightfield& field, const Ecology& ecology, const WorldSize& world,
                       const HorizonGpuParams& params, JobLauncher launch,
                       EditSummarySource edits = {});

    HorizonGpuStreamer(const HorizonGpuStreamer&) = delete;
    HorizonGpuStreamer& operator=(const HorizonGpuStreamer&) = delete;

    // One update around the player (unwrapped Unreal position). Rethrows the errors of the jobs.
    HorizonGpuUpdate update(const UnrealPoint& player, const UnrealFrame& frame);
    // Waits for the epoch in flight and the detail queued; an epoch computed is only committed,
    // and its detail queued, by the next update.
    void waitIdle();
    // Whether an epoch is in flight or some detail is queued, computed or not collected yet.
    [[nodiscard]] bool busy();

    // The parameters of the levels (their crest ratio, even when the crests are deferred).
    [[nodiscard]] const HorizonParams& params() const { return horizonParams_; }
    [[nodiscard]] const HorizonGpuParams& gpuParams() const { return params_; }
    [[nodiscard]] const TileHole& tileHole() const { return hole_; }
    [[nodiscard]] double planetRadiusMeters() const { return radius_; }
    [[nodiscard]] int side() const { return horizonParams_.cellsPerSide + 1; }
    [[nodiscard]] bool shown() const { return runner_.clipmap().placed(); }
    // The samples of the epochs: with deferred crests, their point heights on every level (the
    // texels show the samples raised to their crests once these arrive).
    [[nodiscard]] const HorizonClipmap& clipmap() const { return runner_.clipmap(); }
    [[nodiscard]] std::size_t memoryBytes() const { return runner_.clipmap().memoryBytes(); }
    // The detail normals, or nullptr without them (normalRatio 0).
    [[nodiscard]] HorizonNormalStream* normals() const {
        return detail_ != nullptr && detail_->ratio() > 0 ? detail_.get() : nullptr;
    }
    // The stream of the detail (normals, deferred crests), or nullptr without any.
    [[nodiscard]] HorizonNormalStream* detail() const { return detail_.get(); }

    // The fixed grids: the body of level 0 (with the sea square), the body shared by the levels
    // 1 and up, and the trims (trimGrid).
    [[nodiscard]] GridMesh level0Grid() const;
    [[nodiscard]] GridMesh ringGrid() const;
    // Depth of the skirt and lowest drop of the curvature over the window of a level, meters.
    [[nodiscard]] double skirtMetersOf(int level) const;
    [[nodiscard]] double curveMetersOf(int level) const;

private:
    [[nodiscard]] LevelPlacement placementOf(int level, const CorePoint& observer,
                                             const UnrealFrame& frame) const;
    void placeLevels(const CorePoint& observer, const UnrealFrame& frame, HorizonGpuUpdate& result);
    void collectDetail(HorizonGpuUpdate& result);

    HorizonParams horizonParams_;
    HorizonGpuParams params_;
    TileHole hole_;
    double radius_;
    HorizonSampler sampler_; // of the epochs: crests only when they are not deferred
    std::vector<std::optional<LevelPlacement>> shown_;
    std::optional<std::chrono::steady_clock::time_point> epochStart_;
    std::optional<std::chrono::steady_clock::time_point> detailStart_;
    std::unique_ptr<HorizonNormalStream> detail_; // before runner_: it takes the launcher
    HorizonEpochRunner runner_; // last: its jobs end before the sampler goes away
};

} // namespace terrain::unreal

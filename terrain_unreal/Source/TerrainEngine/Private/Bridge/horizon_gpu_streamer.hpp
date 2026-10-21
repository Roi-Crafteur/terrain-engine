#pragma once

#include "horizon_epochs.hpp"
#include "horizon_layout.hpp"
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
    std::vector<LevelPlacement> placements; // the levels whose placement changed
    // With an epoch committed: the coarsest level it moved (-1: none), whether it showed the
    // levels for the first time, the time from its plan to its commit, the samples it wrote.
    bool committed = false;
    int coarsestMoved = -1;
    bool first = false;
    double epochMilliseconds = 0.0;
    std::int64_t samples = 0;

    [[nodiscard]] bool empty() const { return regions.empty() && placements.empty(); }
};

// The far horizon drawn by the GPU (phase 13, step 2): the epochs of the clipmap of terrain_core
// run as jobs (HorizonEpochRunner), their texels prepared on the jobs; each commit writes the
// texels of the samples entering the windows, and the levels are placed again whenever their
// window, their trim or their image nearest to the player changes (a world turn, the origin
// rebasing). The fixed grids, the textures and the material belong to the renderer. Every method
// must be called from the thread owning the streamer.
class HorizonGpuStreamer {
public:
    // field and ecology (those of the tiles) must outlive the streamer and share the world. Throws
    // std::invalid_argument for invalid parameters, or tiles that do not end inside level 0.
    HorizonGpuStreamer(const Heightfield& field, const Ecology& ecology, const WorldSize& world,
                       const HorizonGpuParams& params, JobLauncher launch);

    HorizonGpuStreamer(const HorizonGpuStreamer&) = delete;
    HorizonGpuStreamer& operator=(const HorizonGpuStreamer&) = delete;

    // One update around the player (unwrapped Unreal position). Rethrows the errors of the jobs.
    HorizonGpuUpdate update(const UnrealPoint& player, const UnrealFrame& frame);
    void waitIdle() { runner_.waitIdle(); }

    [[nodiscard]] const HorizonParams& params() const { return horizonParams_; }
    [[nodiscard]] const HorizonGpuParams& gpuParams() const { return params_; }
    [[nodiscard]] const TileHole& tileHole() const { return hole_; }
    [[nodiscard]] double planetRadiusMeters() const { return radius_; }
    [[nodiscard]] int side() const { return horizonParams_.cellsPerSide + 1; }
    [[nodiscard]] bool shown() const { return runner_.clipmap().placed(); }
    [[nodiscard]] const HorizonClipmap& clipmap() const { return runner_.clipmap(); }
    [[nodiscard]] std::size_t memoryBytes() const { return runner_.clipmap().memoryBytes(); }

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

    HorizonParams horizonParams_;
    HorizonGpuParams params_;
    TileHole hole_;
    double radius_;
    HorizonSampler sampler_;
    std::vector<std::optional<LevelPlacement>> shown_;
    std::optional<std::chrono::steady_clock::time_point> epochStart_;
    HorizonEpochRunner runner_; // last: its jobs end before the sampler goes away
};

} // namespace terrain::unreal

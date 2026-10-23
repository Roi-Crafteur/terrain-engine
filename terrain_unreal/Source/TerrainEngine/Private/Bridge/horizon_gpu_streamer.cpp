#include "horizon_gpu_streamer.hpp"

#include "horizon_mesh.hpp"

#include <algorithm>
#include <utility>

namespace terrain::unreal {

namespace {

HorizonParams horizonParams(const WorldSize& world, const HorizonGpuParams& params) {
    HorizonParams horizon;
    horizon.levels = params.levels;
    horizon.cellsPerSide = params.cellsPerSide;
    horizon.world = world;
    return horizon;
}

std::unique_ptr<HorizonNormalStream> normalStream(const Heightfield& field,
                                                  const HorizonParams& horizon,
                                                  const HorizonGpuParams& params,
                                                  const JobLauncher& launch) {
    if (params.normalRatio == 0) {
        return nullptr;
    }
    HorizonNormalParams normals;
    normals.ratio = params.normalRatio;
    normals.maxJobsInFlight = params.normalJobs;
    return std::make_unique<HorizonNormalStream>(field, horizon, normals, launch);
}

std::int64_t samplesWritten(const HorizonChanges& changes) {
    std::int64_t samples = 0;
    for (const HorizonLevelChange& change : changes.levels) {
        for (const HorizonRect& rect : change.written) {
            samples += rect.count();
        }
    }
    return samples;
}

} // namespace

HorizonGpuStreamer::HorizonGpuStreamer(const Heightfield& field, const Ecology& ecology,
                                       const WorldSize& world, const HorizonGpuParams& params,
                                       JobLauncher launch)
    : horizonParams_(horizonParams(world, params)), params_(params),
      hole_(terrain::unreal::tileHole(horizonParams_, params.lod)),
      radius_(params.planetRadiusMeters > 0.0 ? params.planetRadiusMeters
                                              : terrain::unreal::planetRadiusMeters(world)),
      sampler_(field, ecology, horizonParams_),
      shown_(static_cast<std::size_t>(horizonParams_.levels)),
      normals_(normalStream(field, horizonParams_, params, launch)),
      runner_(sampler_, std::move(launch),
              [sea = params.sea](const HorizonPiece& piece) { return pieceTexels(piece, sea); },
              HorizonEpochParams{params.maxPiecesInFlight, kHorizonPieceSamples}) {}

HorizonGpuUpdate HorizonGpuStreamer::update(const UnrealPoint& player, const UnrealFrame& frame) {
    // Unwrapped: the images near the player are taken around where it stands in Unreal.
    const CorePoint observer = toCore(frame, player);
    const auto now = std::chrono::steady_clock::now();
    const EpochCommit commit = runner_.update(observer.x, observer.z);
    HorizonGpuUpdate result;
    if (!commit.empty()) {
        result.committed = true;
        result.first = !shown_.front().has_value();
        result.samples = samplesWritten(commit.changes);
        for (const HorizonLevelChange& change : commit.changes.levels) {
            if (change.moved) {
                result.coarsestMoved = std::max(result.coarsestMoved, change.level);
            }
        }
        if (epochStart_) {
            const std::chrono::duration<double, std::milli> elapsed = now - *epochStart_;
            result.epochMilliseconds = elapsed.count();
            epochStart_.reset();
        }
        for (const PreparedPiece& piece : commit.pieces) {
            for (TexelRegion& region : texelRegions(piece.level, side(), piece.rect, piece.data)) {
                result.regions.push_back(std::move(region));
            }
        }
        if (normals_) {
            normals_->written(runner_.clipmap(), commit.changes, result.normalRegions);
        }
    }
    if (normals_) {
        normals_->collect(result.normalRegions);
    }
    if (runner_.busy() && !epochStart_) {
        epochStart_ = now; // planned by this update
    }
    placeLevels(observer, frame, result);
    return result;
}

void HorizonGpuStreamer::waitIdle() {
    runner_.waitIdle();
    if (normals_) {
        normals_->waitIdle();
    }
}

GridMesh HorizonGpuStreamer::level0Grid() const {
    const double pitch = levelPitch(horizonParams_, 0);
    const double seaHalfCells = (hole_.halfCells * pitch + hole_.overlapMeters) / pitch;
    return bodyGrid(bodyTopology(horizonParams_.cellsPerSide,
                                 bodyHole(horizonParams_, 0, hole_.halfCells), params_.sea),
                    seaHalfCells);
}

GridMesh HorizonGpuStreamer::ringGrid() const {
    return bodyGrid(bodyTopology(horizonParams_.cellsPerSide,
                                 bodyHole(horizonParams_, 1, hole_.halfCells), false),
                    0.0);
}

double HorizonGpuStreamer::skirtMetersOf(int level) const {
    return skirtMeters(levelPitch(horizonParams_, level));
}

double HorizonGpuStreamer::curveMetersOf(int level) const {
    return curvatureDropMeters(levelReachMeters(horizonParams_, level), radius_);
}

LevelPlacement HorizonGpuStreamer::placementOf(int level, const CorePoint& observer,
                                               const UnrealFrame& frame) const {
    const HorizonClipmap& clipmap = runner_.clipmap();
    const HorizonLevel& window = clipmap.level(level);
    LevelPlacement placement{level, levelOrigin(window, frame, observer), window.firstSlot(), -1,
                             {-1, -1}};
    if (level > 0) {
        placement.trim = trimVariant(finerWindowCells(window, clipmap.level(level - 1)),
                                     horizonParams_.cellsPerSide);
    }
    if (level + 1 < clipmap.levelCount()) {
        const CellRect inCoarser = finerWindowCells(clipmap.level(level + 1), window);
        placement.inCoarser = {inCoarser.i0, inCoarser.k0};
    }
    return placement;
}

// Every level placed again whenever its window, its trim or its image changes.
void HorizonGpuStreamer::placeLevels(const CorePoint& observer, const UnrealFrame& frame,
                                     HorizonGpuUpdate& result) {
    if (!runner_.clipmap().placed()) {
        return;
    }
    for (int level = 0; level < horizonParams_.levels; ++level) {
        const LevelPlacement placement = placementOf(level, observer, frame);
        std::optional<LevelPlacement>& shown = shown_[static_cast<std::size_t>(level)];
        if (!shown || !(*shown == placement)) {
            shown = placement;
            result.placements.push_back(placement);
        }
    }
}

} // namespace terrain::unreal

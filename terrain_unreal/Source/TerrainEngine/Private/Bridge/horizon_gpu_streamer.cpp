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
    horizon.crestRatio = params.crestRatio;
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
                                       JobLauncher launch, HorizonEditMap* edits)
    : horizonParams_(horizonParams(world, params)), params_(params),
      hole_(terrain::unreal::tileHole(horizonParams_, params.lod)),
      radius_(params.planetRadiusMeters > 0.0 ? params.planetRadiusMeters
                                              : terrain::unreal::planetRadiusMeters(world)),
      edits_(edits),
      sampler_(field, ecology, horizonParams_, edits ? edits->source() : EditSummarySource{}),
      shown_(static_cast<std::size_t>(horizonParams_.levels)),
      normals_(normalStream(field, horizonParams_, params, launch)),
      runner_(sampler_, std::move(launch),
              [sea = params.sea](const HorizonPiece& piece) { return pieceTexels(piece, sea); },
              HorizonEpochParams{params.maxPiecesInFlight, kHorizonPieceSamples}, edits) {}

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
        keepEditedPending(result.normalRegions);
    }
    if (runner_.busy() && !epochStart_) {
        epochStart_ = now; // planned by this update
    }
    placeLevels(observer, frame, result);
    return result;
}

// The detail texels of the samples edited over at least half of their footprint, pending.
void HorizonGpuStreamer::keepEditedPending(std::vector<NormalRegion>& regions) const {
    if (edits_ == nullptr || edits_->empty()) {
        return;
    }
    const HorizonClipmap& clipmap = runner_.clipmap();
    const int ratio = normals_->ratio();
    for (NormalRegion& region : regions) {
        const HorizonLevel& level = clipmap.level(region.level);
        const HorizonRect window = level.window();
        const HorizonIndex first = level.firstSlot();
        const double half = level.pitch() / 2.0;
        for (int b = 0; b < region.height; ++b) {
            for (int a = 0; a < region.width; ++a) {
                // The sample of the texel, from its slot.
                const auto i =
                    static_cast<int>(((region.x + a) / ratio - first.x + side()) % side());
                const auto k =
                    static_cast<int>(((region.z + b) / ratio - first.z + side()) % side());
                if ((level.atWindow(i, k).flags & kHorizonEdited) == 0) {
                    continue;
                }
                const HorizonIndex sample =
                    canonicalSample({window.x0 + i, window.z0 + k}, level.counts());
                const FootprintEdits edited =
                    edits_->footprint({region.level, samplePosition(sample.x, level.pitch()),
                                       samplePosition(sample.z, level.pitch()), half});
                if (2 * edited.edited >= edited.columns) {
                    const auto at =
                        static_cast<std::size_t>((b * region.width + a) * kNormalHalves);
                    region.data[at] = kNormalPendingHalf;
                    region.data[at + 1] = kNormalPendingHalf;
                }
            }
        }
    }
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

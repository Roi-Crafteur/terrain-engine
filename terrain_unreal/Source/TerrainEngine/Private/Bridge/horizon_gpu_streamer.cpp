#include "horizon_gpu_streamer.hpp"

#include "horizon_mesh.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <set>
#include <span>
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

// Exactly one of them raises the samples to their crests: the epochs, or the detail after them.
HorizonParams epochParams(const HorizonParams& horizon, const HorizonGpuParams& params) {
    HorizonParams epochs = horizon;
    epochs.crestRatio = params.deferCrests ? 0 : horizon.crestRatio;
    return epochs;
}

std::unique_ptr<HorizonNormalStream> detailStream(const Heightfield& field,
                                                  const HorizonParams& horizon,
                                                  const HorizonGpuParams& params,
                                                  const JobLauncher& launch) {
    HorizonParams detail = horizon;
    detail.crestRatio = params.deferCrests ? horizon.crestRatio : 0;
    if (params.normalRatio == 0 && detail.crestRatio == 0) {
        return nullptr;
    }
    HorizonNormalParams normals;
    normals.ratio = params.normalRatio;
    normals.maxJobsInFlight = params.normalJobs;
    return std::make_unique<HorizonNormalStream>(field, detail, normals, launch);
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
      sampler_(field, ecology, epochParams(horizonParams_, params),
               edits ? edits->source() : EditSummarySource{}),
      shown_(static_cast<std::size_t>(horizonParams_.levels)),
      detail_(detailStream(field, horizonParams_, params, launch)),
      runner_(sampler_, std::move(launch),
              [sea = params.sea](const HorizonPiece& piece) { return pieceUploads(piece, sea); },
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
            // pieceUploads: the texels, then the water levels.
            const std::span<const float> data(piece.data);
            const std::size_t texels = std::min(
                data.size(), static_cast<std::size_t>(piece.rect.count()) * kTexelFloats);
            for (TexelRegion& region :
                 texelRegions(piece.level, side(), piece.rect, data.first(texels))) {
                result.regions.push_back(std::move(region));
            }
            for (WaterRegion& region :
                 waterRegions(piece.level, side(), piece.rect, data.subspan(texels))) {
                result.waterRegions.push_back(std::move(region));
            }
        }
        if (detail_) {
            detail_->written(runner_.clipmap(), commit.changes, result.normalRegions);
            detailStart_ = detailStart_.value_or(now);
        }
        if (normals() != nullptr) {
            refreshEditedNeighbors(commit.changes, result.normalRegions);
        }
    }
    collectDetail(result);
    if (normals() != nullptr) {
        // After the detail of the jobs: the edited samples take the slopes of the grid.
        followEditedGrid(result.normalRegions);
    }
    if (runner_.busy() && !epochStart_) {
        epochStart_ = now; // planned by this update
    }
    placeLevels(observer, frame, result);
    return result;
}

// The detail computed since the last update: the normals, and the texels of the samples whose
// crests rise, after those of the commit.
void HorizonGpuStreamer::collectDetail(HorizonGpuUpdate& result) {
    if (!detail_) {
        return;
    }
    std::vector<CrestBand> crests;
    detail_->collect(result.normalRegions, crests);
    for (const CrestBand& band : crests) {
        for (TexelRegion& region :
             crestRegions(runner_.clipmap().level(band.level), side(), band, params_.sea)) {
            result.regions.push_back(std::move(region));
        }
    }
    if (detailStart_ && !detail_->busy()) {
        const std::chrono::duration<double, std::milli> elapsed =
            std::chrono::steady_clock::now() - *detailStart_;
        result.detailMilliseconds = std::max(elapsed.count(), 1e-6);
        detailStart_.reset();
    }
}

// Whether the detail texels of the sample (i, k) of the window of a level follow the grid: the
// sample is edited over at least half of its footprint.
bool HorizonGpuStreamer::followsGrid(int level, int i, int k) const {
    const HorizonLevel& window = runner_.clipmap().level(level);
    if ((window.atWindow(i, k).flags & kHorizonEdited) == 0) {
        return false;
    }
    const HorizonRect rect = window.window();
    const HorizonIndex sample = canonicalSample({rect.x0 + i, rect.z0 + k}, window.counts());
    const FootprintEdits edited =
        edits_->footprint({level, samplePosition(sample.x, window.pitch()),
                           samplePosition(sample.z, window.pitch()), window.pitch() / 2.0});
    return 2 * edited.edited >= edited.columns;
}

// The texels of the sample (i, k) of the window, written into region from its texel (x, z): the
// slopes of the grid, bilinear between the texels of the 4 samples of its cell, as the tile
// material interpolates them (its smooth slope). The last row and column of the window, whose
// cells no material reads, take their own slopes.
void HorizonGpuStreamer::writeGridSlopes(const HorizonLevel& level, int i, int k,
                                         NormalRegion& region, int x, int z) const {
    const int ratio = normals()->ratio();
    const int last = side() - 1;
    std::array<HorizonTexel, 4> corners; // (i, k), (i + 1, k), (i, k + 1), (i + 1, k + 1)
    for (std::size_t c = 0; c < corners.size(); ++c) {
        const int ci = std::min(i + static_cast<int>(c % 2), last);
        const int ck = std::min(k + static_cast<int>(c / 2), last);
        corners[c] = horizonTexel(level.atWindow(ci, ck), params_.sea);
    }
    const auto half = [](double slope) {
        return halfFromFloat(
            std::clamp(static_cast<float>(slope), -kNormalSlopeLimit, kNormalSlopeLimit));
    };
    for (int b = 0; b < ratio; ++b) {
        for (int a = 0; a < ratio; ++a) {
            const double u = static_cast<double>(a) / ratio;
            const double v = static_cast<double>(b) / ratio;
            const std::array<double, 4> weights = {(1.0 - u) * (1.0 - v), u * (1.0 - v),
                                                   (1.0 - u) * v, u * v};
            double gradX = 0.0;
            double gradZ = 0.0;
            for (std::size_t c = 0; c < corners.size(); ++c) {
                gradX += weights[c] * corners[c].gradX;
                gradZ += weights[c] * corners[c].gradZ;
            }
            const auto at =
                static_cast<std::size_t>(((z + b) * region.width + x + a) * kNormalHalves);
            region.data[at] = half(gradX);
            region.data[at + 1] = half(gradZ);
        }
    }
}

// The detail texels of the samples edited over at least half of their footprint: the slopes of
// the grid, never pending. The material filters the texels bilinearly and takes a slope beyond
// kNormalPendingTest for pending; a real slope mixed with a pending texel by a small weight stays
// below it and lays the normal flat, lit by the sky alone (a line along the corners of a razed
// box).
void HorizonGpuStreamer::followEditedGrid(std::vector<NormalRegion>& regions) const {
    if (edits_ == nullptr || edits_->empty()) {
        return;
    }
    const HorizonClipmap& clipmap = runner_.clipmap();
    const int ratio = normals()->ratio();
    for (NormalRegion& region : regions) {
        const HorizonLevel& level = clipmap.level(region.level);
        const HorizonIndex first = level.firstSlot();
        // A region holds whole samples (normalRegions); the sample of a texel from its slot.
        for (int z = 0; z < region.height; z += ratio) {
            for (int x = 0; x < region.width; x += ratio) {
                const auto i =
                    static_cast<int>(((region.x + x) / ratio - first.x + side()) % side());
                const auto k =
                    static_cast<int>(((region.z + z) / ratio - first.z + side()) % side());
                if (followsGrid(region.level, i, k)) {
                    writeGridSlopes(level, i, k, region, x, z);
                }
            }
        }
    }
}

// The texels of a sample following the grid interpolate toward its neighbors at +x and +z: when
// an epoch writes samples, those of their neighbors toward -x and -z that it did not write are
// sent again (pending here, then followEditedGrid), so that the textures only depend on the
// samples, not on the order of the edits.
void HorizonGpuStreamer::refreshEditedNeighbors(const HorizonChanges& changes,
                                                std::vector<NormalRegion>& regions) const {
    if (edits_ == nullptr || edits_->empty()) {
        return;
    }
    for (const HorizonLevelChange& change : changes.levels) {
        const HorizonRect window = runner_.clipmap().level(change.level).window();
        std::set<std::pair<std::int64_t, std::int64_t>> neighbors;
        for (const HorizonRect& rect : change.written) {
            for (std::int64_t z = rect.z0 - 1; z < rect.z1; ++z) {
                neighbors.insert({rect.x0 - 1, z});
            }
            for (std::int64_t x = rect.x0; x < rect.x1; ++x) {
                neighbors.insert({x, rect.z0 - 1});
            }
        }
        for (const auto& [x, z] : neighbors) {
            const HorizonIndex sample{x, z};
            const bool written = std::ranges::any_of(
                change.written, [&](const HorizonRect& rect) { return rect.contains(sample); });
            if (written || !window.contains(sample) ||
                !followsGrid(change.level, static_cast<int>(x - window.x0),
                             static_cast<int>(z - window.z0))) {
                continue;
            }
            for (NormalRegion& region :
                 pendingRegions(change.level, side(), normals()->ratio(), {x, z, x + 1, z + 1})) {
                regions.push_back(std::move(region));
            }
        }
    }
}

void HorizonGpuStreamer::waitIdle() {
    runner_.waitIdle();
    if (detail_) {
        detail_->waitIdle();
    }
}

bool HorizonGpuStreamer::busy() {
    return runner_.busy() || (detail_ != nullptr && detail_->busy());
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

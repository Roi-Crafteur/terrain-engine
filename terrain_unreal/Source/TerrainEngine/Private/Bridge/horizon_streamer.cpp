#include "horizon_streamer.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace terrain::unreal {

namespace {

HorizonParams horizonParams(const WorldSize& world, const HorizonMeshParams& params) {
    HorizonParams horizon;
    horizon.levels = params.levels;
    horizon.cellsPerSide = params.cellsPerSide;
    horizon.crestRatio = params.crestRatio;
    horizon.world = world;
    return horizon;
}

double radiusOf(const WorldSize& world, const HorizonMeshParams& params) {
    return params.planetRadiusMeters > 0.0 ? params.planetRadiusMeters : planetRadiusMeters(world);
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

HorizonMeshStreamer::HorizonMeshStreamer(const Heightfield& field, const Ecology& ecology,
                                         const WorldSize& world, const HorizonMeshParams& params,
                                         HorizonEditMap* edits)
    : horizonParams_(horizonParams(world, params)), look_(params.look),
      hole_(terrain::unreal::tileHole(horizonParams_, params.lod)),
      radius_(radiusOf(world, params)),
      sampler_(field, ecology, horizonParams_, edits ? edits->source() : EditSummarySource{}),
      streamer_(sampler_, {params.workerThreads, kHorizonPieceSamples}, edits) {
    for (int level = 0; level < horizonParams_.levels; ++level) {
        topologies_.push_back(bodyTopology(horizonParams_.cellsPerSide,
                                           bodyHole(horizonParams_, level, hole_.halfCells),
                                           level == 0 && look_.sea));
    }
    bodyShown_.assign(topologies_.size(), false);
    origins_.resize(topologies_.size());
}

HorizonUpdate HorizonMeshStreamer::update(const UnrealPoint& player, const UnrealFrame& frame) {
    // Unwrapped: the images near the player are taken around where it stands in Unreal.
    const CorePoint observer = toCore(frame, player);
    const auto now = std::chrono::steady_clock::now();
    const HorizonChanges changes = streamer_.update(observer.x, observer.z);
    HorizonUpdate result;
    if (!changes.empty()) {
        result.committed = true;
        result.first = !origins_.front().has_value();
        result.samples = samplesWritten(changes);
        if (epochStart_) {
            const std::chrono::duration<double, std::milli> elapsed = now - *epochStart_;
            result.epochMilliseconds = elapsed.count();
            epochStart_.reset();
        }
        convertChanges(changes, observer, frame, result);
    }
    if (streamer_.busy() && !epochStart_) {
        epochStart_ = now; // planned by this update
    }
    placeLevels(observer, frame, result);
    return result;
}

std::size_t HorizonMeshStreamer::memoryBytes() const {
    std::size_t bytes = streamer_.clipmap().memoryBytes();
    for (const BodyTopology& topology : topologies_) {
        bytes += (topology.samples.size() + topology.border.size() + topology.triangles.size()) *
                 sizeof(std::int32_t);
    }
    return bytes;
}

BodyShape HorizonMeshStreamer::shapeOf(int level) const {
    const int pitch = levelPitch(horizonParams_, level);
    BodyShape shape;
    shape.coarser = level + 1 < horizonParams_.levels ? &streamer_.level(level + 1) : nullptr;
    shape.skirtMeters = skirtMeters(pitch);
    shape.seaHalfMeters = level == 0 ? hole_.halfCells * pitch + hole_.overlapMeters : 0.0;
    shape.curveMeters = curvatureDropMeters(levelReachMeters(horizonParams_, level), radius_);
    return shape;
}

HorizonUpload HorizonMeshStreamer::body(int level, const UnrealPoint& origin) {
    const auto start = std::chrono::steady_clock::now();
    const auto index = static_cast<std::size_t>(level);
    BodyTopology& topology = topologies_[index];
    HorizonUpload upload{level, HorizonSection::Body, origin, {}, !bodyShown_[index], 0.0};
    fillBody(streamer_.level(level), topology, shapeOf(level), look_, upload.mesh);
    if (upload.newTriangles) {
        // Unreal keeps them: the next uploads of the body only replace its vertices.
        upload.mesh.triangles = std::exchange(topology.triangles, {});
        bodyShown_[index] = true;
    }
    upload.conversionMilliseconds = millisecondsSince(start);
    return upload;
}

HorizonUpload HorizonMeshStreamer::trim(int level, const UnrealPoint& origin) const {
    const auto start = std::chrono::steady_clock::now();
    const HorizonLevel& coarse = streamer_.level(level);
    const CellRect square = bodyHole(horizonParams_, level, hole_.halfCells);
    const CellRect finer = finerWindowCells(coarse, streamer_.level(level - 1));
    HorizonUpload upload{level, HorizonSection::Trim, origin,
                         levelTrim(coarse, square, finer, look_), true, 0.0};
    upload.conversionMilliseconds = millisecondsSince(start);
    return upload;
}

// Whether the samples a commit wrote in a level meet the window of the next finer one, whose
// border is stitched to them: an edit may write a level and not the finer one (the samples
// entering a moved window never meet it).
bool HorizonMeshStreamer::writesUnderFiner(const HorizonLevelChange& change) const {
    if (change.level == 0) {
        return false;
    }
    const HorizonLevel& coarse = streamer_.level(change.level);
    const CellRect finer = finerWindowCells(coarse, streamer_.level(change.level - 1));
    const HorizonRect window = coarse.window();
    for (const HorizonRect& rect : change.written) {
        if (rect.x0 - window.x0 <= finer.i1 && rect.x1 - window.x0 > finer.i0 &&
            rect.z0 - window.z0 <= finer.k1 && rect.z1 - window.z0 > finer.k0) {
            return true;
        }
    }
    return false;
}

// The bodies of the levels the epoch changed, and of the finer levels stitched to samples it
// wrote (writesUnderFiner), the trims of those and of the next coarser levels (around their
// moved finer windows).
void HorizonMeshStreamer::convertChanges(const HorizonChanges& changes, const CorePoint& observer,
                                         const UnrealFrame& frame, HorizonUpdate& result) {
    const std::size_t levels = topologies_.size();
    std::vector<bool> bodies(levels, false);
    std::vector<bool> trims(levels, false);
    for (const HorizonLevelChange& change : changes.levels) {
        const auto level = static_cast<std::size_t>(change.level);
        bodies[level] = true;
        if (writesUnderFiner(change)) {
            bodies[level - 1] = true;
        }
        trims[level] = level > 0;
        if (level + 1 < levels) {
            trims[level + 1] = true;
        }
        if (change.moved) {
            result.coarsestMoved = std::max(result.coarsestMoved, change.level);
        }
    }
    for (std::size_t level = 0; level < levels; ++level) {
        if (!bodies[level] && !trims[level]) {
            continue;
        }
        const int n = static_cast<int>(level);
        const UnrealPoint origin = levelOrigin(streamer_.level(n), frame, observer);
        if (bodies[level]) {
            result.uploads.push_back(body(n, origin));
        }
        if (trims[level]) {
            result.uploads.push_back(trim(n, origin));
        }
        origins_[level] = origin;
    }
}

// The levels shown follow the player to their nearest images, and the frame of Unreal.
void HorizonMeshStreamer::placeLevels(const CorePoint& observer, const UnrealFrame& frame,
                                      HorizonUpdate& result) {
    for (std::size_t level = 0; level < origins_.size(); ++level) {
        if (!origins_[level]) {
            continue;
        }
        const int n = static_cast<int>(level);
        const UnrealPoint origin = levelOrigin(streamer_.level(n), frame, observer);
        if (!(origin == *origins_[level])) {
            origins_[level] = origin;
            result.moves.push_back({n, origin});
        }
    }
}

} // namespace terrain::unreal

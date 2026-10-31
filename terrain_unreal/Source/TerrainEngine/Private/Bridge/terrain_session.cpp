#include "terrain_session.hpp"

#include "terrain/generation/caves.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include <chrono>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <utility>

namespace terrain::unreal {

namespace {

std::optional<CaveParams> caveParams(const SessionParams& params) {
    if (!params.caves) {
        return std::nullopt;
    }
    CaveParams caves;
    if (params.caveRegions) {
        caves.districts = CaveDistrictParams{};
    }
    return caves;
}

// The planet level of the file, read or built (openPlanetSource), or none without a file.
PlanetSource planetSource(const SessionParams& params) {
    if (params.planetFile.empty()) {
        return {};
    }
    const std::filesystem::path file(reinterpret_cast<const char8_t*>(params.planetFile.c_str()));
    return openPlanetSource(file, params.seed, params.world);
}

// The hydrology of the planet level, or none without one.
std::shared_ptr<const Hydrology> hydrology(const SessionParams& params,
                                           const PlanetSource& planet) {
    if (planet.level == nullptr) {
        return nullptr;
    }
    const Heightfield relief(params.seed, HeightfieldParams{}, params.world);
    return std::make_shared<const Hydrology>(params.seed, relief, planet.level);
}

StreamParams streamParams(const SessionParams& params) {
    StreamParams stream = params.stream;
    stream.world = params.world;
    return stream;
}

} // namespace

TerrainSession::TerrainSession(const SessionParams& params)
    : planet_(planetSource(params)),
      tiles_(params.seed, HeightfieldParams{}, EcologyParams{}, caveParams(params), params.world,
             hydrology(params, planet_)),
      timings_(std::make_shared<TileTimings>()), horizonEdits_(tiles_.heightfield()),
      streamer_(timedGenerator(tileGenerator(tiles_), timings_), streamParams(params), timings_) {
}

std::vector<ChunkCoord> TerrainSession::applyEdit(const VolumeEdit& edit) {
    finishQueued();
    return edited(tiles_.applyEdit(edit));
}

std::vector<ChunkCoord> TerrainSession::applyInSequence(std::span<const SequencedEdit> edits) {
    finishQueued();
    return edited(tiles_.applyInSequence(edits));
}

std::vector<ChunkCoord> TerrainSession::setBlock(BlockCoord block, BlockEdit edit) {
    finishQueued();
    return edited(tiles_.setBlock(block, edit));
}

std::vector<ChunkCoord> TerrainSession::clearBlock(BlockCoord block) {
    finishQueued();
    return edited(tiles_.clearBlock(block));
}

std::vector<ChunkCoord> TerrainSession::replaceModifications(ModificationStore store) {
    finishQueued();
    return edited(tiles_.replaceModifications(std::move(store)));
}

std::vector<ChunkCoord> TerrainSession::edited(std::vector<ChunkCoord> chunks) {
    streamer_.invalidate(chunks);
    horizonEdits_.stage(tiles_, chunks);
    return chunks;
}

void TerrainSession::queueEdit(const VolumeEdit& edit, std::string label) {
    editThread_.push([this, edit, label = std::move(label)]() mutable {
        return runEdit([&edit] { return edit; }, std::move(label));
    });
}

void TerrainSession::queueGroundEdit(const GroundEditCommand& command, double x, double z,
                                     std::string label) {
    editThread_.push([this, command, x, z, label = std::move(label)]() mutable {
        return runEdit([&] { return groundEditAt(tiles_.heightfield(), command, x, z); },
                       std::move(label));
    });
}

// The queued edits done and staged, before an edit on this thread: the order of the edits.
void TerrainSession::finishQueued() {
    editThread_.wait();
    stageDone();
}

std::vector<EditOutcome> TerrainSession::takeEdits() {
    stageDone();
    return std::exchange(done_, {});
}

// On the edit thread: the volume of an edit, the edit of the tiles (under their lock), then the
// ground of the columns of its chunks as HorizonEditMap::stage reads it, from the edits as they
// are right after it (the next edit waits on this thread).
EditOutcome TerrainSession::runEdit(const std::function<VolumeEdit()>& volume, std::string label) {
    EditOutcome outcome;
    outcome.label = std::move(label);
    const auto start = std::chrono::steady_clock::now();
    try {
        outcome.volume = volume();
        outcome.chunks = tiles_.applyEdit(outcome.volume);
    } catch (...) {
        outcome.error = std::current_exception();
        return outcome;
    }
    outcome.editMilliseconds = millisecondsSince(start);
    const auto read = std::chrono::steady_clock::now();
    outcome.columns = editedColumns(tiles_.heightfield(), outcome.chunks,
                                    [this](int minX, int maxX, int minZ, int maxZ) {
                                        return tiles_.editWindow(minX, maxX, minZ, maxZ);
                                    });
    outcome.columnMilliseconds = millisecondsSince(read);
    return outcome;
}

// The edits done on the edit thread, in their order: their tiles invalidated, their ground staged.
void TerrainSession::stageDone() {
    for (EditOutcome& outcome : editThread_.take()) {
        const auto start = std::chrono::steady_clock::now();
        if (outcome.error == nullptr) {
            streamer_.invalidate(outcome.chunks);
            horizonEdits_.stage(outcome.columns);
        }
        outcome.stageMilliseconds = millisecondsSince(start);
        done_.push_back(std::move(outcome));
    }
}

UnrealFrame TerrainSession::frameAt(double x, double z) const {
    return {{x, static_cast<double>(tiles_.heightfield().heightAt(x, z)), z}};
}

double TerrainSession::groundZ(const UnrealFrame& frame, const UnrealPoint& point) const {
    const CorePoint core = toCore(frame, point);
    // The top of the rock (an overhang lies above heightAt there).
    const double ground = tiles_.groundAt(core.x, core.z);
    return toUnreal(frame, {core.x, ground, core.z}).z;
}

} // namespace terrain::unreal

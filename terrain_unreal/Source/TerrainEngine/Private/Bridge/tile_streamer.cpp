#include "tile_streamer.hpp"

#include <stdexcept>
#include <thread>
#include <utility>

namespace terrain::unreal {

TileStreamer::TileStreamer(TileGenerator generator, const StreamParams& params,
                           std::shared_ptr<TileTimings> timings)
    : timings_(std::move(timings)), manager_(std::move(generator), params) {}

StreamerUpdate TileStreamer::update(const UnrealPoint& player, const UnrealFrame& frame) {
    const CorePoint position = toCore(frame, player);
    const StreamChanges changes = manager_.update(position.x, position.z);
    StreamerUpdate result;
    result.uploads.reserve(changes.loaded.size());
    for (const TileKey& key : changes.loaded) {
        result.uploads.push_back(convert(key, frame));
        shown_.insert(key);
    }
    for (const TileKey& key : changes.unloaded) {
        shown_.erase(key);
    }
    result.removals = changes.unloaded;
    return result;
}

WarmUpResult TileStreamer::warmUp(const UnrealPoint& player, const UnrealFrame& frame,
                                  int radius, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    const ChunkCoord chunk = chunkAt(frame, player);
    WarmUpResult result;
    for (;;) {
        StreamerUpdate changes = update(player, frame);
        if (!changes.empty()) {
            result.updates.push_back(std::move(changes));
        }
        if (nearTilesShown(chunk, radius)) {
            result.complete = true;
            return result;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return result;
        }
        if (manager_.generatePending(1) == 0) { // the workers hold the remaining tiles
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

TileUpload TileStreamer::convert(const TileKey& key, const UnrealFrame& frame) const {
    const TileData* data = manager_.find(key);
    if (data == nullptr) {
        throw std::logic_error("TileStreamer: a loaded tile has no data");
    }
    const auto start = std::chrono::steady_clock::now();
    TileUpload upload{key, tileOrigin(frame, *data), convertMesh(data->mesh, data->originY),
                      hasCollision(key), 0.0};
    upload.conversionMilliseconds = millisecondsSince(start);
    if (timings_ != nullptr) {
        timings_->record(TileStage::Conversion, key.lod, upload.conversionMilliseconds);
        timings_->recordMesh(key.lod, upload.mesh.positions.size(), upload.mesh.triangleCount());
    }
    return upload;
}

bool TileStreamer::nearTilesShown(ChunkCoord player, int radius) const {
    for (const TileRequest& request : manager_.selection()) {
        const TileKey& key = request.key;
        if (key.lod == 0 && tileDistance(key, player) <= radius && !shown(key)) {
            return false;
        }
    }
    return true;
}

} // namespace terrain::unreal

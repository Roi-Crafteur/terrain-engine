#include "tile_streamer.hpp"

#include "tile_blend_atlas.hpp"

#include <stdexcept>
#include <thread>
#include <utility>

namespace terrain::unreal {

TileStreamer::TileStreamer(TileGenerator generator, const StreamParams& params,
                           std::shared_ptr<TileTimings> timings)
    : timings_(std::move(timings)), manager_(std::move(generator), params) {}

StreamerUpdate TileStreamer::update(const UnrealPoint& player, const UnrealFrame& frame) {
    // Unwrapped: the images near the player are taken around where it stands in Unreal.
    const CorePoint position = toCore(frame, player);
    const ChunkCoord chunk = worldToChunk(position.x, position.z);
    const StreamChanges changes = manager_.update(position.x, position.z);
    StreamerUpdate result;
    for (const TileKey& key : changes.unloaded) {
        shown_.erase(key);
    }
    placeShown(chunk, frame, result);
    result.uploads.reserve(changes.loaded.size());
    for (const TileKey& key : changes.loaded) {
        const TileKey image = imageNear(key, chunk);
        result.uploads.push_back(convert(key, image, frame));
        shown_.insert_or_assign(key, image);
    }
    result.removals = changes.unloaded;
    return result;
}

TileKey TileStreamer::shownImage(const TileKey& key) const {
    const auto it = shown_.find(key);
    return it == shown_.end() ? key : it->second;
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

TileUpload TileStreamer::convert(const TileKey& key, const TileKey& image,
                                 const UnrealFrame& frame) const {
    const TileData* data = manager_.find(key);
    if (data == nullptr) {
        throw std::logic_error("TileStreamer: a loaded tile has no data");
    }
    const auto start = std::chrono::steady_clock::now();
    const TileSea sea{&data->surfaceBlend, blendPitchMeters(key.lod)};
    TileUpload upload{key,
                      tileOrigin(frame, *data, image),
                      convertMesh(data->mesh, data->originY, data->waterLevels, sea),
                      hasCollision(key),
                      0.0,
                      data->surfaceBlend,
                      buildTileWater(*data),
                      buildFallCurtains(*data)};
    upload.conversionMilliseconds = millisecondsSince(start);
    if (timings_ != nullptr) {
        timings_->record(TileStage::Conversion, key.lod, upload.conversionMilliseconds);
        timings_->recordMesh(key.lod, upload.mesh.positions.size(), upload.mesh.triangleCount());
    }
    return upload;
}

TileMove TileStreamer::move(const TileKey& key, const TileKey& image,
                           const UnrealFrame& frame) const {
    const TileData* data = manager_.find(key);
    if (data == nullptr) {
        throw std::logic_error("TileStreamer: a shown tile has no data");
    }
    return {key, tileOrigin(frame, *data, image)};
}

TileKey TileStreamer::imageNear(const TileKey& key, ChunkCoord player) const {
    return nearestTile(key, player, manager_.params().world);
}

// The tiles already shown follow the player to their nearest image. Within half a world of where
// they were shown nothing moves: only a jump of the player across the world moves them.
void TileStreamer::placeShown(ChunkCoord player, const UnrealFrame& frame,
                              StreamerUpdate& result) {
    for (auto& [key, image] : shown_) {
        const TileKey nearest = imageNear(key, player);
        if (nearest != image) {
            image = nearest;
            result.moves.push_back(move(key, nearest, frame));
        }
    }
}

bool TileStreamer::nearTilesShown(ChunkCoord player, int radius) const {
    for (const TileRequest& request : manager_.selection()) {
        const TileKey& key = request.key;
        const int distance = tileDistance(imageNear(key, player), player);
        if (key.lod == 0 && distance <= radius && !shown(key)) {
            return false;
        }
    }
    return true;
}

} // namespace terrain::unreal

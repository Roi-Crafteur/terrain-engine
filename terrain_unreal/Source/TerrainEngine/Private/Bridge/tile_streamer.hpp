#pragma once

#include "tile_mesh_conversion.hpp"
#include "tile_timings.hpp"
#include "unreal_frame.hpp"

#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/stream_manager.hpp"

#include <chrono>
#include <cstddef>
#include <memory>
#include <unordered_set>
#include <vector>

namespace terrain::unreal {

// Tiles of this LOD get collision: the tiles nearest to the player (LodParams::splitDistance[0]).
inline constexpr int kCollisionLod = 0;

[[nodiscard]] constexpr bool hasCollision(const TileKey& key) {
    return key.lod == kCollisionLod;
}

// A tile to show: its mesh placed at its origin, with collision or not. A tile already shown
// under this key is replaced in place.
struct TileUpload {
    TileKey key;
    UnrealPoint origin;
    UnrealMesh mesh;
    bool collision = false;
    double conversionMilliseconds = 0.0;
};

// What the meshes of Unreal must do after one update: the uploads, then the removals. A tile is
// removed only once the tiles replacing it are uploaded (StreamManager): applying a whole update
// within one frame never leaves a hole.
struct StreamerUpdate {
    std::vector<TileUpload> uploads;
    std::vector<TileKey> removals;

    [[nodiscard]] bool empty() const { return uploads.empty() && removals.empty(); }
};

struct WarmUpResult {
    std::vector<StreamerUpdate> updates; // to apply in order
    bool complete = false;               // false: the timeout ran out first
};

// The streaming of an Unreal level: a StreamManager whose loaded tiles are converted for
// Unreal as soon as they are loaded (the data of a tile is only valid until the next update).
// The budget of conversions per update is the budget of loads of the manager
// (StreamParams::maxLoadsPerUpdate). Every method must be called from the thread owning it.
class TileStreamer {
public:
    // The timings, optional, receive the conversions. Throws like StreamManager.
    TileStreamer(TileGenerator generator, const StreamParams& params,
                 std::shared_ptr<TileTimings> timings = nullptr);

    TileStreamer(const TileStreamer&) = delete;
    TileStreamer& operator=(const TileStreamer&) = delete;

    // One update of the streaming around the player (an Unreal position). Rethrows the errors of
    // the generator, as StreamManager::update.
    StreamerUpdate update(const UnrealPoint& player, const UnrealFrame& frame);

    // Updates, generating on the calling thread one tile at a time (the workers may hold others),
    // until every LOD 0 tile within radius chunks of the player is shown, or until the timeout:
    // the ground under a new player before it can fall.
    WarmUpResult warmUp(const UnrealPoint& player, const UnrealFrame& frame, int radius,
                        std::chrono::milliseconds timeout);

    // Generates up to maxTiles queued tiles on the calling thread (StreamManager), for a
    // streaming without workers; the next update loads them.
    std::size_t generatePending(std::size_t maxTiles) { return manager_.generatePending(maxTiles); }

    // Shown tiles: uploaded by an update and not removed since.
    [[nodiscard]] bool shown(const TileKey& key) const { return shown_.contains(key); }
    [[nodiscard]] std::size_t shownCount() const { return shown_.size(); }
    [[nodiscard]] const StreamManager& manager() const { return manager_; }

private:
    [[nodiscard]] TileUpload convert(const TileKey& key, const UnrealFrame& frame) const;
    [[nodiscard]] bool nearTilesShown(ChunkCoord player, int radius) const;

    std::shared_ptr<TileTimings> timings_;
    StreamManager manager_;
    std::unordered_set<TileKey, TileKeyHash> shown_;
};

} // namespace terrain::unreal

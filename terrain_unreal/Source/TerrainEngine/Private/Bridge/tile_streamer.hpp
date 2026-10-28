#pragma once

#include "tile_mesh_conversion.hpp"
#include "tile_timings.hpp"
#include "unreal_frame.hpp"

#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/stream_manager.hpp"

#include <chrono>
#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>

namespace terrain::unreal {

// Tiles of this LOD get collision: the tiles nearest to the player (LodParams::splitDistance[0]).
inline constexpr int kCollisionLod = 0;

[[nodiscard]] constexpr bool hasCollision(const TileKey& key) {
    return key.lod == kCollisionLod;
}

// A tile to show: its mesh placed at its origin, with collision or not. The key is canonical
// (wrapTile); the origin is the one of its image nearest to the player (nearestTile). A tile
// already shown under this key is replaced in place. Its surface blend (TileData::surfaceBlend,
// tile_blend_atlas.hpp) goes to the materials as it is.
struct TileUpload {
    TileKey key;
    UnrealPoint origin;
    UnrealMesh mesh;
    bool collision = false;
    double conversionMilliseconds = 0.0;
    std::vector<BlendTexel> blend;
};

// A shown tile whose nearest image changed (the player moved by about half a world, a teleport
// across the world): its mesh moves to this origin, by whole worlds, unchanged otherwise.
struct TileMove {
    TileKey key;
    UnrealPoint origin;
};

// What the meshes of Unreal must do after one update: the uploads, the moves, then the removals.
// A tile is removed only once the tiles replacing it are uploaded (StreamManager): applying a
// whole update within one frame never leaves a hole.
struct StreamerUpdate {
    std::vector<TileUpload> uploads;
    std::vector<TileMove> moves;
    std::vector<TileKey> removals;

    [[nodiscard]] bool empty() const {
        return uploads.empty() && moves.empty() && removals.empty();
    }
};

struct WarmUpResult {
    std::vector<StreamerUpdate> updates; // to apply in order
    bool complete = false;               // false: the timeout ran out first
};

// The streaming of an Unreal level: a StreamManager whose loaded tiles are converted for
// Unreal as soon as they are loaded (the data of a tile is only valid until the next update).
// The budget of conversions per update is the budget of loads of the manager
// (StreamParams::maxLoadsPerUpdate). Every method must be called from the thread owning it.
//
// The world wraps around (StreamParams::world) but the Unreal world does not: the player keeps
// its unwrapped position, which the manager brings back into the canonical range, and each tile
// is shown at its image nearest to the chunk of the player, so that walking across an edge of the
// world shows the tiles beyond it side by side with the others.
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

    // Shown tiles: uploaded by an update and not removed since, by canonical key.
    [[nodiscard]] bool shown(const TileKey& key) const { return shown_.contains(key); }
    [[nodiscard]] std::size_t shownCount() const { return shown_.size(); }
    // The image a shown tile is shown at; the key itself for a tile not shown.
    [[nodiscard]] TileKey shownImage(const TileKey& key) const;
    [[nodiscard]] const StreamManager& manager() const { return manager_; }

private:
    [[nodiscard]] TileUpload convert(const TileKey& key, const TileKey& image,
                                     const UnrealFrame& frame) const;
    [[nodiscard]] TileMove move(const TileKey& key, const TileKey& image,
                                const UnrealFrame& frame) const;
    [[nodiscard]] TileKey imageNear(const TileKey& key, ChunkCoord player) const;
    void placeShown(ChunkCoord player, const UnrealFrame& frame, StreamerUpdate& result);
    [[nodiscard]] bool nearTilesShown(ChunkCoord player, int radius) const;

    std::shared_ptr<TileTimings> timings_;
    StreamManager manager_;
    std::unordered_map<TileKey, TileKey, TileKeyHash> shown_; // canonical key -> image shown
};

} // namespace terrain::unreal

// Streamer of the Unreal plugin, on cheap tiles: the budget of conversions per update, nearest
// tiles first, collision on LOD 0 only, a random walk after which the simulated Unreal meshes
// always match the loaded tiles (replacements in place included), the wrapped world (a walk
// across its edge with every tile at its image nearest to the player, a jump of a whole world
// that only moves the meshes, the return of a far player by whole worlds, a warm-up at its
// corner), the warm-up under a new player with busy
// workers and its timeout, the errors of the generator, and determinism.
#include "tile_blend_atlas.hpp"
#include "tile_streamer.hpp"

#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using terrain::ChunkCoord;
using terrain::StreamParams;
using terrain::TileData;
using terrain::TileKey;
using terrain::TileKeyHash;
using terrain::TileRequest;
using terrain::unreal::StreamerUpdate;
using terrain::unreal::TileMove;
using terrain::unreal::TileStreamer;
using terrain::unreal::TileUpload;
using terrain::unreal::UnrealFrame;
using terrain::unreal::UnrealPoint;

// A cheap tile, a pure function of its request: one counter-clockwise triangle whose first
// vertex holds the seams, at the altitude of its LOD.
TileData fakeTile(const TileRequest& request) {
    TileData data;
    data.request = request;
    data.originX = request.key.origin.x * terrain::kChunkSizeMeters;
    data.originY = request.key.lod;
    data.originZ = request.key.origin.z * terrain::kChunkSizeMeters;
    data.mesh.vertices = {{static_cast<float>(request.seams), 0.0f, 0.0f},
                          {0.0f, 0.0f, 1.0f},
                          {1.0f, 0.0f, 0.0f}};
    data.mesh.normals.assign(3, {0.0f, 1.0f, 0.0f});
    data.mesh.indices = {0, 1, 2};
    return data;
}

StreamParams smallParams(int workers, std::size_t loadsPerUpdate) {
    StreamParams params;
    params.lod = streaming_test::smallLod();
    params.workerThreads = workers;
    params.maxLoadsPerUpdate = loadsPerUpdate;
    return params;
}

const UnrealFrame kFrame{{100.0, 20.0, -200.0}};

// Unreal position of the center of a chunk.
UnrealPoint centerOf(ChunkCoord chunk) {
    return terrain::unreal::toUnreal(kFrame, {(chunk.x + 0.5) * terrain::kChunkSizeMeters, 0.0,
                                              (chunk.z + 0.5) * terrain::kChunkSizeMeters});
}

// The meshes of Unreal, simulated: what each shown tile displays.
using Shown = std::unordered_map<TileKey, TileUpload, TileKeyHash>;

// Applies an update as the Unreal side does (uploads, moves, removals), counting the tiles
// replaced in place. False if it moves or removes a tile that is not shown.
bool apply(Shown& shown, StreamerUpdate update, std::size_t& replaced) {
    for (TileUpload& upload : update.uploads) {
        replaced += shown.contains(upload.key) ? 1 : 0;
        const TileKey key = upload.key;
        shown.insert_or_assign(key, std::move(upload));
    }
    bool known = true;
    for (const TileMove& move : update.moves) {
        const auto it = shown.find(move.key);
        known = it != shown.end() && known;
        if (it != shown.end()) {
            it->second.origin = move.origin;
        }
    }
    for (const TileKey& key : update.removals) {
        known = shown.erase(key) == 1 && known;
    }
    return known;
}

// One frame: up to generate tiles generated on this thread, then the update.
StreamerUpdate step(TileStreamer& streamer, ChunkCoord player, std::size_t generate) {
    streamer.generatePending(generate);
    return streamer.update(centerOf(player), kFrame);
}

// The image of a tile nearest to the player, within reach of its view (a tile leaving the
// selection stays loaded until the tiles overlapping it are: one coarsest tile further at most).
bool shownNear(const TileStreamer& streamer, const TileKey& key, ChunkCoord player) {
    const terrain::StreamParams& params = streamer.manager().params();
    const TileKey image = streamer.shownImage(key);
    const int reach = params.lod.viewDistance + terrain::tileChunks(terrain::kMaxLod);
    return image == terrain::nearestTile(key, player, params.world) &&
           terrain::tileDistance(image, player) <= reach;
}

// The shown meshes are those of the loaded tiles (canonical keys), converted, each at its image
// nearest to the player.
bool matchesLoaded(const Shown& shown, const TileStreamer& streamer, ChunkCoord player) {
    const std::vector<TileKey> loaded = streamer.manager().loadedTiles();
    if (loaded.size() != shown.size() || streamer.shownCount() != shown.size()) {
        return false;
    }
    for (const TileKey& key : loaded) {
        const auto it = shown.find(key);
        const TileData* data = streamer.manager().find(key);
        if (it == shown.end() || data == nullptr || !streamer.shown(key) ||
            key != terrain::wrapTile(key, streamer.manager().params().world) ||
            !shownNear(streamer, key, player) ||
            it->second.mesh != terrain::unreal::convertMesh(
                                   data->mesh, data->originY, data->waterLevels,
                                   {&data->surfaceBlend,
                                    terrain::unreal::blendPitchMeters(key.lod)}) ||
            it->second.origin !=
                terrain::unreal::tileOrigin(kFrame, *data, streamer.shownImage(key)) ||
            it->second.collision != (key.lod == 0)) {
            return false;
        }
    }
    return true;
}

// At most maxLoadsPerUpdate conversions per update, nearest first, until every selected tile
// is shown, with collision on the LOD 0 tiles only.
void testBudgetNearestFirst() {
    constexpr std::size_t kPerUpdate = 5;
    TileStreamer streamer(fakeTile, smallParams(0, kPerUpdate));
    const ChunkCoord player{3, -7};
    TEST_CHECK(streamer.update(centerOf(player), kFrame).empty()); // nothing generated yet
    Shown shown;
    std::size_t replaced = 0;
    bool bounded = true;
    bool nearestFirst = true;
    int lastDistance = 0;
    for (int round = 0; round < 1000 && streamer.manager().pendingCount() > 0; ++round) {
        StreamerUpdate update = step(streamer, player, 1000);
        bounded = bounded && update.uploads.size() <= kPerUpdate && update.removals.empty();
        for (const TileUpload& upload : update.uploads) {
            const int distance = terrain::tileDistance(upload.key, player);
            nearestFirst = nearestFirst && distance >= lastDistance;
            lastDistance = distance;
        }
        TEST_CHECK(apply(shown, std::move(update), replaced));
    }
    TEST_CHECK(bounded);
    TEST_CHECK(nearestFirst);
    TEST_CHECK(replaced == 0);
    TEST_CHECK(shown.size() == streamer.manager().selection().size());
    TEST_CHECK(shown.contains(TileKey{player, 0}) && shown.at(TileKey{player, 0}).collision);
    TEST_CHECK(matchesLoaded(shown, streamer, player));
}

// A deterministic walk: small moves and, every 20 frames, a jump far away.
ChunkCoord walk(ChunkCoord player, int frame, std::uint64_t& state) {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    const auto move = [&state](int shift) { return static_cast<int>((state >> shift) % 7) - 3; };
    if (frame % 20 == 19) {
        return {player.x + 40, player.z - 25};
    }
    return {player.x + move(33), player.z + move(45)};
}

// After every frame of a walk, the simulated meshes are exactly the loaded tiles.
void testRandomWalk() {
    TileStreamer streamer(fakeTile, smallParams(0, 7));
    Shown shown;
    std::size_t replaced = 0;
    bool known = true;
    bool matching = true;
    std::uint64_t state = 2026;
    ChunkCoord player{0, 0};
    for (int frame = 0; frame < 80; ++frame) {
        player = walk(player, frame, state);
        known = apply(shown, step(streamer, player, 9), replaced) && known;
        matching = matching && matchesLoaded(shown, streamer, player);
    }
    TEST_CHECK(known);
    TEST_CHECK(matching);
    TEST_CHECK(replaced > 0); // the seams of tiles kept by the walk changed
}

// Generates and loads every selected tile around the player, applying each update.
bool loadAll(TileStreamer& streamer, Shown& shown, ChunkCoord player) {
    std::size_t replaced = 0;
    bool known = apply(shown, step(streamer, player, 0), replaced);
    for (int round = 0; round < 100 && streamer.manager().pendingCount() > 0; ++round) {
        known = apply(shown, step(streamer, player, 1000), replaced) && known;
    }
    return known && streamer.manager().pendingCount() == 0;
}

// A walk east across the edge x = world.x / 2: the keys stay canonical, every tile is shown at its
// image nearest to the player, so the tiles beyond the edge (canonical keys at the west end of the
// world) stand next to the others, and nothing ever moves.
void testWalkAcrossTheEdge() {
    TileStreamer streamer(fakeTile, smallParams(0, 1000));
    const ChunkCoord chunks = terrain::worldChunks(streamer.manager().params().world);
    Shown shown;
    bool matching = true;
    bool crossing = false;
    bool still = true;
    for (int x = chunks.x / 2 - 12; x <= chunks.x / 2 + 12; x += 3) {
        const ChunkCoord player{x, 40};
        std::size_t replaced = 0;
        StreamerUpdate update = step(streamer, player, 1000);
        still = still && update.moves.empty();
        matching = apply(shown, std::move(update), replaced) && matching;
        matching = loadAll(streamer, shown, player) && matchesLoaded(shown, streamer, player) &&
                   matching;
        for (const auto& [key, upload] : shown) {
            const TileKey image = streamer.shownImage(key);
            crossing = crossing || (key.origin.x < 0 && image.origin.x > 0) ||
                       (key.origin.x > 0 && image.origin.x < 0);
        }
    }
    TEST_CHECK(matching);
    TEST_CHECK(crossing);
    TEST_CHECK(still);
}

// A jump of exactly one world east: the manager sees the same canonical place, nothing is loaded
// or removed, and every mesh moves one world east, exactly.
void testJumpByAWorld() {
    TileStreamer streamer(fakeTile, smallParams(0, 1000));
    const terrain::WorldSize world = streamer.manager().params().world;
    const ChunkCoord player{-1234, 5678};
    Shown shown;
    TEST_CHECK(loadAll(streamer, shown, player));
    const Shown before = shown;
    const ChunkCoord jumped{player.x + terrain::worldChunks(world).x, player.z};
    const StreamerUpdate update = streamer.update(centerOf(jumped), kFrame);
    TEST_CHECK(update.uploads.empty() && update.removals.empty());
    TEST_CHECK(update.moves.size() == shown.size());
    bool byAWorld = true;
    for (const TileMove& move : update.moves) {
        const UnrealPoint old = before.at(move.key).origin;
        byAWorld = byAWorld && move.origin.x == old.x + world.x * 100.0 &&
                   move.origin.y == old.y && move.origin.z == old.z;
    }
    TEST_CHECK(byAWorld);
    std::size_t replaced = 0;
    TEST_CHECK(apply(shown, update, replaced));
    TEST_CHECK(matchesLoaded(shown, streamer, jumped));
}

// A player beyond the threshold of a world turn, east and north, moved back by worldTurnOffset:
// nothing is loaded or removed, and every mesh moves by the same offset, exactly, so nothing moves
// relative to the player.
void testWorldTurn() {
    TileStreamer streamer(fakeTile, smallParams(0, 1000));
    const terrain::WorldSize world = streamer.manager().params().world;
    const ChunkCoord chunks = terrain::worldChunks(world);
    const ChunkCoord player{chunks.x * 4 / 5, -chunks.z * 4 / 5};
    Shown shown;
    TEST_CHECK(loadAll(streamer, shown, player));
    const Shown before = shown;
    const UnrealPoint position = centerOf(player);
    const UnrealPoint offset = terrain::unreal::worldTurnOffset(position, world);
    TEST_CHECK(offset.x == -world.x * 100.0 && offset.y == world.z * 100.0 && offset.z == 0.0);
    const UnrealPoint moved{position.x + offset.x, position.y + offset.y, position.z};
    const StreamerUpdate update = streamer.update(moved, kFrame);
    TEST_CHECK(update.uploads.empty() && update.removals.empty());
    TEST_CHECK(update.moves.size() == shown.size());
    bool withThePlayer = true;
    for (const TileMove& move : update.moves) {
        const UnrealPoint old = before.at(move.key).origin;
        withThePlayer = withThePlayer && move.origin.x == old.x + offset.x &&
                        move.origin.y == old.y + offset.y && move.origin.z == old.z;
    }
    TEST_CHECK(withThePlayer);
    std::size_t replaced = 0;
    TEST_CHECK(apply(shown, update, replaced));
    TEST_CHECK(streamer.update(moved, kFrame).empty());
}

// At the corner of the world, the warm-up waits for the LOD 0 tiles on the four sides of the
// corner, each at its image next to the player.
void testWarmUpAtTheCorner() {
    TileStreamer streamer(fakeTile, smallParams(0, 4));
    const terrain::WorldSize world = streamer.manager().params().world;
    const ChunkCoord chunks = terrain::worldChunks(world);
    const ChunkCoord player{chunks.x / 2, -chunks.z / 2}; // just past the north-east corner
    const terrain::unreal::WarmUpResult warm =
        streamer.warmUp(centerOf(player), kFrame, 1, std::chrono::seconds(10));
    TEST_CHECK(warm.complete);
    bool around = true;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const TileKey key =
                terrain::wrapTile({{player.x + dx, player.z + dz}, 0}, world);
            around = around && streamer.shown(key) && shownNear(streamer, key, player) &&
                     streamer.shownImage(key) == TileKey{{player.x + dx, player.z + dz}, 0};
        }
    }
    TEST_CHECK(around);
}

// The warm-up returns once the LOD 0 tiles around the player are shown, even when a worker holds
// the slow tile of the player; its updates, applied in order, give the shown tiles.
void testWarmUpWithBusyWorkers() {
    const ChunkCoord player{-12, 30};
    const terrain::TileGenerator slowCenter = [player](const TileRequest& request) {
        if (request.key == TileKey{player, 0}) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return fakeTile(request);
    };
    TileStreamer streamer(slowCenter, smallParams(2, 4));
    const terrain::unreal::WarmUpResult warm =
        streamer.warmUp(centerOf(player), kFrame, 1, std::chrono::seconds(10));
    TEST_CHECK(warm.complete);
    bool nearShown = true;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            nearShown = nearShown && streamer.shown(TileKey{{player.x + dx, player.z + dz}, 0});
        }
    }
    TEST_CHECK(nearShown);
    Shown shown;
    std::size_t replaced = 0;
    bool known = true;
    for (const StreamerUpdate& update : warm.updates) {
        known = apply(shown, update, replaced) && known;
    }
    TEST_CHECK(known);
    TEST_CHECK(shown.size() == streamer.shownCount());
}

// When the tiles cannot be generated in time, the warm-up says so.
void testWarmUpTimeout() {
    const terrain::TileGenerator slow = [](const TileRequest& request) {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        return fakeTile(request);
    };
    TileStreamer streamer(slow, smallParams(1, 16));
    const terrain::unreal::WarmUpResult warm =
        streamer.warmUp(centerOf({5, 5}), kFrame, 1, std::chrono::milliseconds(5));
    TEST_CHECK(!warm.complete);
    TEST_CHECK(!streamer.shown(TileKey{{4, 4}, 0}) || !streamer.shown(TileKey{{6, 6}, 0}));
}

// An error of the generator is rethrown by the next update, and by a warm-up.
void testGeneratorError() {
    const ChunkCoord player{1, 1};
    const terrain::TileGenerator failing = [player](const TileRequest& request) {
        if (request.key == TileKey{player, 0}) {
            throw std::runtime_error("generation failed");
        }
        return fakeTile(request);
    };
    TileStreamer streamer(failing, smallParams(0, 16));
    static_cast<void>(streamer.update(centerOf(player), kFrame));
    TEST_CHECK(streamer.generatePending(1) == 1); // the nearest tile: the one that fails
    bool rethrown = false;
    try {
        static_cast<void>(streamer.update(centerOf(player), kFrame));
    } catch (const std::runtime_error&) {
        rethrown = true;
    }
    TEST_CHECK(rethrown);
    TileStreamer fresh(failing, smallParams(0, 16));
    bool warmUpRethrown = false;
    try {
        static_cast<void>(fresh.warmUp(centerOf(player), kFrame, 1, std::chrono::seconds(5)));
    } catch (const std::runtime_error&) {
        warmUpRethrown = true;
    }
    TEST_CHECK(warmUpRethrown);
}

bool sameUpdates(const StreamerUpdate& a, const StreamerUpdate& b) {
    if (a.uploads.size() != b.uploads.size() || a.removals != b.removals) {
        return false;
    }
    for (std::size_t i = 0; i < a.uploads.size(); ++i) {
        const TileUpload& x = a.uploads[i];
        const TileUpload& y = b.uploads[i];
        if (x.key != y.key || x.origin != y.origin || x.mesh != y.mesh ||
            x.collision != y.collision) {
            return false;
        }
    }
    return true;
}

// The same walk gives the same updates.
void testDeterministic() {
    TileStreamer a(fakeTile, smallParams(0, 6));
    TileStreamer b(fakeTile, smallParams(0, 6));
    std::uint64_t stateA = 7;
    std::uint64_t stateB = 7;
    ChunkCoord playerA{2, 2};
    ChunkCoord playerB{2, 2};
    bool same = true;
    for (int frame = 0; frame < 40; ++frame) {
        playerA = walk(playerA, frame, stateA);
        playerB = walk(playerB, frame, stateB);
        same = same && sameUpdates(step(a, playerA, 8), step(b, playerB, 8));
    }
    TEST_CHECK(same);
}

} // namespace

int main() {
    testBudgetNearestFirst();
    testRandomWalk();
    testWalkAcrossTheEdge();
    testJumpByAWorld();
    testWorldTurn();
    testWarmUpAtTheCorner();
    testWarmUpWithBusyWorkers();
    testWarmUpTimeout();
    testGeneratorError();
    testDeterministic();
    return test::result();
}

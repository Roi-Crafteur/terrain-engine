// Stream manager: priority by distance, progressive loading, unloading of far tiles, tiles kept
// until replaced, stale results dropped, invalidation, errors, and the same tiles and data
// whatever the number of worker threads. Edits made through TerrainTiles: persistence_test.cpp.
#include "terrain/streaming/stream_manager.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace {

using terrain::ChunkCoord;
using terrain::StreamChanges;
using terrain::StreamManager;
using terrain::StreamParams;
using terrain::TileData;
using terrain::TileKey;
using terrain::TileKeyHash;
using terrain::TileRequest;

// A cheap tile, a pure function of its request.
TileData fakeTile(const TileRequest& request) {
    TileData data;
    data.request = request;
    data.originX = request.key.origin.x * terrain::kChunkSizeMeters;
    data.originZ = request.key.origin.z * terrain::kChunkSizeMeters;
    data.mesh.vertices.push_back(
        {static_cast<float>(request.key.lod), static_cast<float>(request.seams), 0.0f});
    return data;
}

StreamParams smallParams(int workers, std::size_t loadsPerUpdate = 1000) {
    StreamParams params;
    params.lod = streaming_test::smallLod();
    params.workerThreads = workers;
    params.maxLoadsPerUpdate = loadsPerUpdate;
    return params;
}

// Center of a chunk, in meters.
double center(int chunk) {
    return (chunk + 0.5) * terrain::kChunkSizeMeters;
}

StreamChanges at(StreamManager& manager, ChunkCoord player) {
    return manager.update(center(player.x), center(player.z));
}

// Generates and loads every selected tile around the player.
void loadAll(StreamManager& manager, ChunkCoord player) {
    at(manager, player);
    for (int round = 0; round < 1000 && manager.pendingCount() > 0; ++round) {
        manager.waitIdle();
        at(manager, player);
    }
}

std::unordered_set<TileKey, TileKeyHash> keysOf(const std::vector<TileRequest>& requests) {
    std::unordered_set<TileKey, TileKeyHash> keys;
    for (const TileRequest& request : requests) {
        keys.insert(request.key);
    }
    return keys;
}

bool loadedIsSelection(const StreamManager& manager) {
    const std::vector<TileKey> loaded = manager.loadedTiles();
    const auto selected = keysOf(manager.selection());
    return loaded.size() == selected.size() &&
           std::all_of(loaded.begin(), loaded.end(),
                       [&selected](const TileKey& key) { return selected.contains(key); });
}

bool throwsOnConstruction(const StreamParams& params) {
    try {
        const StreamManager manager(fakeTile, params);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

void testInvalidParams() {
    TEST_CHECK(!throwsOnConstruction(smallParams(0)));
    TEST_CHECK(throwsOnConstruction(smallParams(-1)));
    TEST_CHECK(throwsOnConstruction(smallParams(0, 0)));
    StreamParams badLod = smallParams(0);
    badLod.lod.splitDistance = {4, 5, 16};
    TEST_CHECK(throwsOnConstruction(badLod));
}

// The queue is generated nearest first: the tile of the player, then by distance.
void testPriorityByDistance() {
    std::vector<TileKey> order;
    StreamManager manager(
        [&order](const TileRequest& request) {
            order.push_back(request.key);
            return fakeTile(request);
        },
        smallParams(0));
    const ChunkCoord player{-7, 12};
    const StreamChanges first = at(manager, player);
    TEST_CHECK(first.empty()); // nothing is generated yet
    TEST_CHECK(manager.pendingCount() == manager.selection().size());
    while (manager.generatePending(1) == 1) {
    }
    TEST_CHECK(order.size() == manager.selection().size());
    bool sameOrder = order.size() == manager.selection().size();
    bool nearestFirst = true;
    for (std::size_t i = 0; sameOrder && i < order.size(); ++i) {
        sameOrder = order[i] == manager.selection()[i].key;
        nearestFirst = nearestFirst && (i == 0 || terrain::tileDistance(order[i - 1], player) <=
                                                      terrain::tileDistance(order[i], player));
    }
    TEST_CHECK(sameOrder);
    TEST_CHECK(nearestFirst);
    TEST_CHECK(order.front() == (TileKey{player, 0}));
}

// At most maxLoadsPerUpdate tiles are loaded per update, nearest first.
void testProgressiveLoading() {
    constexpr std::size_t kPerUpdate = 5;
    StreamManager manager(fakeTile, smallParams(0, kPerUpdate));
    const ChunkCoord player{3, 3};
    at(manager, player);
    manager.waitIdle();
    const std::size_t total = manager.selection().size();
    std::vector<TileKey> loadedOrder;
    bool bounded = true;
    for (std::size_t round = 0; round < total; ++round) {
        const StreamChanges changes = at(manager, player);
        bounded = bounded && changes.loaded.size() <= kPerUpdate && changes.unloaded.empty();
        loadedOrder.insert(loadedOrder.end(), changes.loaded.begin(), changes.loaded.end());
    }
    TEST_CHECK(bounded);
    TEST_CHECK(loadedOrder.size() == total);
    bool nearestFirst = loadedOrder.size() == total;
    for (std::size_t i = 0; nearestFirst && i < total; ++i) {
        nearestFirst = loadedOrder[i] == manager.selection()[i].key;
    }
    TEST_CHECK(nearestFirst);
    TEST_CHECK(manager.pendingCount() == 0 && loadedIsSelection(manager));
    TEST_CHECK(at(manager, player).empty());
}

// Tiles far from the new position, which no selected tile overlaps, are unloaded at once.
void testUnloadFarTiles() {
    StreamManager manager(fakeTile, smallParams(2));
    const ChunkCoord near{0, 0};
    loadAll(manager, near);
    const std::size_t before = manager.loadedCount();
    TEST_CHECK(before == manager.selection().size() && loadedIsSelection(manager));

    const ChunkCoord far{200, -150};
    const StreamChanges changes = at(manager, far);
    TEST_CHECK(changes.unloaded.size() == before);
    TEST_CHECK(manager.loadedCount() <= changes.loaded.size());
    loadAll(manager, far);
    TEST_CHECK(loadedIsSelection(manager));
    bool withinView = true;
    for (const TileKey& key : manager.loadedTiles()) {
        withinView = withinView && terrain::tileDistance(key, far) <=
                                       manager.params().lod.viewDistance;
    }
    TEST_CHECK(withinView);
}

// A tile leaving the selection while the LODs change stays loaded until the selected tiles over
// it are loaded: no hole in the terrain.
void testTilesKeptUntilReplaced() {
    StreamManager manager(fakeTile, smallParams(0));
    const ChunkCoord start{0, 0};
    loadAll(manager, start);
    const std::vector<TileKey> before = manager.loadedTiles();

    const ChunkCoord moved{5, 0};
    const StreamChanges changes = at(manager, moved);
    TEST_CHECK(changes.loaded.empty());
    const auto selected = keysOf(manager.selection());
    std::size_t retiring = 0;
    for (const TileKey& key : before) {
        retiring += !selected.contains(key) && manager.find(key) != nullptr ? 1 : 0;
    }
    TEST_CHECK(retiring > 0);
    // Every chunk within both views is still covered by a loaded tile.
    const std::vector<TileKey> loaded = manager.loadedTiles();
    const int reach = manager.params().lod.viewDistance;
    bool noHole = true;
    for (int z = -reach; z <= reach; ++z) {
        for (int x = moved.x - reach; x <= start.x + reach; ++x) {
            noHole = noHole && std::any_of(loaded.begin(), loaded.end(), [x, z](const TileKey& k) {
                         return terrain::tileContains(k, {x, z});
                     });
        }
    }
    TEST_CHECK(noHole);

    loadAll(manager, moved);
    TEST_CHECK(loadedIsSelection(manager));
}

// Generated tiles that are no longer selected, or whose ticket changed, are never loaded.
void testStaleResultsDropped() {
    StreamManager manager(fakeTile, smallParams(0));
    at(manager, {0, 0});
    manager.waitIdle(); // generated for (0, 0), not loaded yet
    const ChunkCoord far{500, 500};
    const StreamChanges changes = at(manager, far);
    TEST_CHECK(changes.loaded.empty() && manager.loadedCount() == 0);
    loadAll(manager, far);
    TEST_CHECK(loadedIsSelection(manager));
}

// An invalidated tile keeps its old data until its new data is loaded.
void testInvalidateKeepsOldTile() {
    std::atomic<int> version{0};
    StreamManager manager(
        [&version](const TileRequest& request) {
            TileData data = fakeTile(request);
            data.minHeight = static_cast<float>(version.load());
            return data;
        },
        smallParams(0));
    const ChunkCoord player{1, 1};
    loadAll(manager, player);
    const TileKey edited{player, 0};
    TEST_CHECK(manager.find(edited) != nullptr && manager.find(edited)->minHeight == 0.0f);

    version = 1;
    const ChunkCoord chunks[] = {player};
    manager.invalidate(chunks);
    TEST_CHECK(manager.pendingCount() == 1);
    TEST_CHECK(manager.find(edited) != nullptr && manager.find(edited)->minHeight == 0.0f);
    TEST_CHECK(manager.generatePending(100) == 1);
    const StreamChanges changes = at(manager, player);
    TEST_CHECK(changes.loaded.size() == 1 && changes.loaded.front() == edited);
    TEST_CHECK(manager.find(edited)->minHeight == 1.0f && manager.pendingCount() == 0);
    // The other tiles kept their data.
    const TileKey other = manager.selection().back().key;
    TEST_CHECK(manager.find(other)->minHeight == 0.0f);
}

void testGeneratorErrorRethrown() {
    StreamManager manager(
        [](const TileRequest& request) -> TileData {
            if (request.key.lod == terrain::kMaxLod) {
                throw std::runtime_error("generator failure");
            }
            return fakeTile(request);
        },
        smallParams(2));
    at(manager, {0, 0});
    bool thrown = false;
    try {
        manager.waitIdle();
        at(manager, {0, 0});
    } catch (const std::runtime_error&) {
        thrown = true;
    }
    TEST_CHECK(thrown);
}

// The same tiles and data with no worker (synchronous) and with three, at two positions.
void testDeterministicAcrossThreads() {
    const terrain::TerrainTiles tiles(streaming_test::kSeed);
    StreamManager sync(terrain::tileGenerator(tiles), smallParams(0, 8));
    StreamManager async(terrain::tileGenerator(tiles), smallParams(3, 8));
    for (const ChunkCoord player : {ChunkCoord{-20, 31}, ChunkCoord{-14, 33}}) {
        loadAll(sync, player);
        loadAll(async, player);
        TEST_CHECK(sync.loadedTiles() == async.loadedTiles());
        TEST_CHECK(loadedIsSelection(sync) && loadedIsSelection(async));
        bool same = true;
        for (const TileRequest& request : sync.selection()) {
            const TileData* a = sync.find(request.key);
            const TileData* b = async.find(request.key);
            same = same && a != nullptr && b != nullptr && *a == *b && a->request == request;
        }
        TEST_CHECK(same);
        // And the same as generating the tile directly.
        const TileRequest& first = sync.selection().front();
        TEST_CHECK(*sync.find(first.key) == tiles.generate(first));
    }
}

} // namespace

int main() {
    testInvalidParams();
    testPriorityByDistance();
    testProgressiveLoading();
    testUnloadFarTiles();
    testTilesKeptUntilReplaced();
    testStaleResultsDropped();
    testInvalidateKeepsOldTile();
    testGeneratorErrorRethrown();
    testDeterministicAcrossThreads();
    return test::result();
}

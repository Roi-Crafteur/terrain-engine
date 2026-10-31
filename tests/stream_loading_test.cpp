// The rhythm of the loads of StreamManager (agent 38): the order of the requests and of the
// integrations on a case made by hand (nearest first), the budget of loads and the limit of the
// workers changed during a load, no tile lost nor loaded twice, and the same tiles, bit for bit,
// whatever the threads and the limits.
#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include "terrain/streaming/stream_manager.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <unordered_map>
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

double center(int chunk) {
    return (chunk + 0.5) * terrain::kChunkSizeMeters;
}

TileData fakeTile(const TileRequest& request) {
    TileData data;
    data.request = request;
    data.originX = request.key.origin.x * terrain::kChunkSizeMeters;
    data.originZ = request.key.origin.z * terrain::kChunkSizeMeters;
    data.mesh.vertices = {{static_cast<float>(request.seams), 0.0f, 0.0f}};
    return data;
}

StreamParams params(int workers, std::size_t loads) {
    StreamParams result;
    result.lod = streaming_test::smallLod();
    result.workerThreads = workers;
    result.maxLoadsPerUpdate = loads;
    return result;
}

// The generations in their order, from any thread.
struct Recorder {
    std::mutex mutex;
    std::vector<TileKey> generated;

    terrain::TileGenerator wrap(terrain::TileGenerator generator) {
        return [this, generator = std::move(generator)](const TileRequest& request) {
            {
                const std::lock_guard lock(mutex);
                generated.push_back(request.key);
            }
            return generator(request);
        };
    }
};

// The 9 tiles of the ground around the chunk (0, 0), in the order of loadsBefore: the tile under
// the player, then the 8 around it row by row (distance 1, LOD 0, z then x).
const std::vector<TileKey> kGroundOrder = {
    {{0, 0}, 0},  {{-1, -1}, 0}, {{0, -1}, 0}, {{1, -1}, 0}, {{-1, 0}, 0},
    {{1, 0}, 0},  {{-1, 1}, 0},  {{0, 1}, 0},  {{1, 1}, 0}};

// Without worker, generated one at a time and loaded 4 per update: the requests and the
// integrations both follow the order made by hand, then go on nearest first.
void testOrderByHand() {
    Recorder recorder;
    StreamManager manager(recorder.wrap(fakeTile), params(0, 4));
    std::vector<TileKey> integrated;
    manager.update(center(0), center(0));
    for (int round = 0; round < 1000 && manager.pendingCount() > 0; ++round) {
        manager.generatePending(3);
        const StreamChanges changes = manager.update(center(0), center(0));
        TEST_CHECK(changes.loaded.size() <= 4);
        integrated.insert(integrated.end(), changes.loaded.begin(), changes.loaded.end());
    }
    TEST_CHECK(recorder.generated.size() >= kGroundOrder.size());
    TEST_CHECK(std::vector<TileKey>(recorder.generated.begin(),
                                    recorder.generated.begin() + 9) == kGroundOrder);
    TEST_CHECK(std::vector<TileKey>(integrated.begin(), integrated.begin() + 9) == kGroundOrder);
    // Nearest first all along: the distance never decreases.
    int previous = 0;
    for (const TileKey& key : integrated) {
        const int distance = terrain::tileDistance(key, {0, 0});
        TEST_CHECK(distance >= previous);
        previous = distance;
    }
    TEST_CHECK(integrated.size() == manager.selection().size());
}

// A budget of loads changed during the load takes effect at the next update; 0 is refused.
void testBudgetChanged() {
    StreamManager manager(fakeTile, params(0, 2));
    manager.update(center(0), center(0));
    manager.generatePending(100);
    TEST_CHECK(manager.update(center(0), center(0)).loaded.size() == 2);
    manager.setMaxLoadsPerUpdate(16);
    TEST_CHECK(manager.params().maxLoadsPerUpdate == 16);
    TEST_CHECK(manager.update(center(0), center(0)).loaded.size() == 16);
    bool thrown = false;
    try {
        manager.setMaxLoadsPerUpdate(0);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    TEST_CHECK(thrown && manager.params().maxLoadsPerUpdate == 16);
}

// The limit of the workers, clamped to the workers; nothing without workers.
void testWorkerLimitClamped() {
    StreamManager manager(fakeTile, params(4, 8));
    TEST_CHECK(manager.workerLimit() == 4);
    manager.setWorkerLimit(0);
    TEST_CHECK(manager.workerLimit() == 1);
    manager.setWorkerLimit(99);
    TEST_CHECK(manager.workerLimit() == 4);
    StreamManager synchronous(fakeTile, params(0, 8));
    synchronous.setWorkerLimit(3);
    TEST_CHECK(synchronous.workerLimit() == 0);
}

using Loaded = std::map<std::tuple<int, int, int>, TileData>;

// Loads every tile around the player, the limits changed along the way (limits[i] at update i,
// while they last), and returns them; false through `once` if a tile was loaded twice.
Loaded loadAround(terrain::TileGenerator generator, ChunkCoord player, int workers,
                  const std::vector<std::pair<int, std::size_t>>& limits, bool& once) {
    StreamManager manager(std::move(generator), params(workers, 4));
    std::unordered_map<TileKey, int, TileKeyHash> loads;
    for (std::size_t round = 0; round < 100000; ++round) {
        if (round < limits.size()) {
            manager.setWorkerLimit(limits[round].first);
            manager.setMaxLoadsPerUpdate(limits[round].second);
        }
        const StreamChanges changes = manager.update(center(player.x), center(player.z));
        for (const TileKey& key : changes.loaded) {
            ++loads[key];
        }
        if (manager.pendingCount() == 0) {
            break;
        }
        if (workers == 0) {
            manager.generatePending(2);
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    }
    Loaded result;
    once = manager.pendingCount() == 0 && loads.size() == manager.selection().size();
    for (const auto& [key, count] : loads) {
        once = once && count == 1 && manager.find(key) != nullptr;
        result.emplace(std::tuple(key.origin.x, key.origin.z, key.lod), *manager.find(key));
    }
    return result;
}

// The real tiles of the world around a place: without worker, with 4 workers limited to 1, then
// 4, then 2, then 4 with budgets of 1 to 32 loads: the same tiles, each loaded once, bit for bit.
void testSameTilesWhateverTheLimits() {
    const terrain::TerrainTiles tiles(streaming_test::kSeed);
    const ChunkCoord player{streaming_test::kCenterChunkX + 3, streaming_test::kCenterChunkZ - 5};
    bool onceReference = false;
    const Loaded reference =
        loadAround(terrain::tileGenerator(tiles), player, 0, {}, onceReference);
    TEST_CHECK(onceReference && !reference.empty());
    const std::vector<std::pair<int, std::size_t>> limits = {
        {1, 1}, {1, 1}, {4, 32}, {4, 32}, {2, 3}, {2, 3}, {2, 3}, {4, 8}};
    bool onceLimited = false;
    const Loaded limited =
        loadAround(terrain::tileGenerator(tiles), player, 4, limits, onceLimited);
    TEST_CHECK(onceLimited);
    TEST_CHECK(limited == reference);
}

} // namespace

int main() {
    testOrderByHand();
    testBudgetChanged();
    testWorkerLimitClamped();
    testSameTilesWhateverTheLimits();
    return test::result();
}

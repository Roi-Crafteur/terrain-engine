// The threads of the plugin stopped in the middle of the courses of the rivers (agent 39): the
// cache of the dev map, the reads of the ground of a teleport, the GPU horizon and the workers of
// the tiles, each destroyed while its threads compute courses of a hydrology that has computed
// nothing yet (seconds of work), then all four at once in the order of ATerrainWorld::ReleaseWorld
// (the end of the Play, Terrain.Seed). Their work leaves by WorkStopped (work_stop.hpp), never
// finishing; the median time of a destruction is 100 ms at most, the longest a quarter of the
// time the work in progress would still have taken, at most. The times are printed.
#include "ground_read.hpp"
#include "horizon_epochs.hpp"
#include "map_tile_cache.hpp"

#include "hydrology_sample_support.hpp"
#include "map_world_support.hpp"
#include "test_assert.hpp"

#include "terrain/streaming/stream_manager.hpp"
#include "terrain/work_stop.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

using namespace terrain;
using namespace terrain::unreal;

namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point from) {
    return std::chrono::duration<double, std::milli>(Clock::now() - from).count();
}

// Jobs on threads of their own, as Unreal runs the jobs of the GPU horizon on its tasks.
class Pool {
public:
    explicit Pool(int threads) {
        for (int i = 0; i < threads; ++i) {
            workers_.emplace_back([this] { loop(); });
        }
    }
    ~Pool() {
        {
            const std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        ready_.notify_all();
        for (std::thread& t : workers_) {
            t.join();
        }
    }
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    [[nodiscard]] JobLauncher launcher() {
        return [this](std::function<void()> job) {
            {
                const std::lock_guard lock(mutex_);
                jobs_.push_back(std::move(job));
            }
            ready_.notify_one();
        };
    }

private:
    void loop() {
        std::unique_lock lock(mutex_);
        for (;;) {
            ready_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (jobs_.empty()) {
                return;
            }
            std::function<void()> job = std::move(jobs_.front());
            jobs_.pop_front();
            lock.unlock();
            job();
            lock.lock();
        }
    }

    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::function<void()>> jobs_;
    bool stopping_ = false;
    std::vector<std::thread> workers_;
};

// Seconds of work on a fresh hydrology: kHeavyReaches courses, from a drawn reach on. Counts the
// works started, finished, and left by WorkStopped.
constexpr std::size_t kHeavyReaches = 150;
struct Heavy {
    const Hydrology* h = nullptr;
    std::shared_ptr<std::atomic<int>> started = std::make_shared<std::atomic<int>>(0);
    std::shared_ptr<std::atomic<int>> finished = std::make_shared<std::atomic<int>>(0);
    std::shared_ptr<std::atomic<int>> stopped = std::make_shared<std::atomic<int>>(0);

    void operator()(std::size_t from) const {
        started->fetch_add(1);
        try {
            const std::size_t n = h->reachCount();
            for (std::size_t k = 0; k < kHeavyReaches; ++k) {
                (void)h->reach((from * 7919 + k * 31) % n);
            }
        } catch (const WorkStopped&) {
            stopped->fetch_add(1);
            throw;
        }
        finished->fetch_add(1);
    }
};

struct Run {
    double destroyMs = 0.0;
    bool allStopped = false; // every work started left by WorkStopped
    int started = 0;
    int stopped = 0;
};

// The median destruction, and checks every run.
double check(const char* what, const std::vector<Run>& runs, double workMs) {
    std::vector<double> ms;
    for (const Run& r : runs) {
        ms.push_back(r.destroyMs);
        std::printf("  %s: %.2f ms, %d works started, %d stopped\n", what, r.destroyMs, r.started,
                    r.stopped);
        TEST_CHECK(r.allStopped);
        TEST_CHECK(r.destroyMs < 0.25 * workMs);
    }
    std::sort(ms.begin(), ms.end());
    std::printf("%s: destroyed in %.2f ms (median), %.2f ms at most, the work %.0f ms\n", what,
                ms[ms.size() / 2], ms.back(), workMs);
    TEST_CHECK(ms[ms.size() / 2] <= 100.0);
    return ms[ms.size() / 2];
}

constexpr int kTries = 3;
constexpr int kStartMs = 60;

// What the stops are measured on: the small world, a fresh hydrology for each try; and the same
// world without water, for the samples of the horizon (its pieces sampled at once, their heavy
// work that of their preparation, counted).
struct Session {
    map_world::WetWorld world;
    Heavy heavy{world.hydrology.get()};
    TerrainTiles dry{map_world::kSeed, HeightfieldParams{}, EcologyParams{}, std::nullopt,
                     golden::kSmallPlanet};

    [[nodiscard]] bool allStopped() const {
        return heavy.started->load() > 0 && heavy.finished->load() == 0 &&
               heavy.stopped->load() == heavy.started->load();
    }
};

std::unique_ptr<MapTileCache> mapCache(const Session& s) {
    const Heavy heavy = s.heavy;
    auto render = [heavy](const MapTileKey& key, const std::vector<std::string>&, MapCancel) {
        heavy(static_cast<std::size_t>(key.tx + 3 * key.tz));
        return MapImage{};
    };
    auto probe = [heavy](double, double) {
        heavy(11);
        return MapProbe{};
    };
    auto cache = std::make_unique<MapTileCache>(render, probe, std::vector<std::string>{},
                                                MapCacheParams{8, 2});
    cache->want({{3, 0, 0}, {3, 1, 0}});
    return cache;
}

std::unique_ptr<StreamManager> tileWorkers(const Session& s) {
    const Heavy heavy = s.heavy;
    StreamParams params;
    params.workerThreads = 2;
    params.world = golden::kSmallPlanet;
    auto manager = std::make_unique<StreamManager>(
        [heavy](const TileRequest& request) {
            heavy(static_cast<std::size_t>(request.key.origin.x + 5));
            return TileData{};
        },
        params);
    (void)manager->update(0.0, 0.0);
    return manager;
}

std::unique_ptr<HorizonEpochRunner> horizonRunner(const Session& s, const HorizonSampler& sampler,
                                                  Pool& pool) {
    const Heavy heavy = s.heavy;
    auto runner = std::make_unique<HorizonEpochRunner>(
        sampler, pool.launcher(),
        [heavy](const HorizonPiece& piece) {
            heavy(static_cast<std::size_t>(piece.rect.z0 + 13 * piece.level));
            return std::vector<float>{};
        },
        HorizonEpochParams{2, 4096});
    (void)runner->update(0.0, 0.0);
    return runner;
}

// The read of the ground of a teleport near the first fall: the first of a fresh hydrology.
std::unique_ptr<GroundRead> groundRead(const Session& s) {
    const map_world::FallPlace fall = map_world::firstFall();
    auto read = std::make_unique<GroundRead>();
    read->ask(s.world.tiles, fall.x, fall.z);
    return read;
}

// The work of one Heavy alone, never stopped: what a destruction without stops waits for.
double workMs() {
    const Session s;
    const Clock::time_point start = Clock::now();
    s.heavy(0);
    return msSince(start);
}

template <class Make>
std::vector<Run> stops(Make&& make) {
    std::vector<Run> runs;
    for (int k = 0; k < kTries; ++k) {
        const Session s;
        auto running = make(s);
        std::this_thread::sleep_for(std::chrono::milliseconds(kStartMs));
        const Clock::time_point start = Clock::now();
        running.reset();
        runs.push_back({msSince(start), s.allStopped(), s.heavy.started->load(),
                        s.heavy.stopped->load()});
    }
    return runs;
}

const HorizonParams& horizonParams() {
    static const HorizonParams params = [] {
        HorizonParams p = hydrology_sample::horizonParams();
        p.levels = 1; // a piece sampled at once: the heavy work is that of its preparation
        p.cellsPerSide = 16;
        return p;
    }();
    return params;
}

// The four at once, destroyed in the order of ReleaseWorld: the map, the ground reads (Loading),
// the GPU horizon, the workers of the tiles (the session).
std::vector<Run> releaseWorld() {
    std::vector<Run> runs;
    for (int k = 0; k < kTries; ++k) {
        const Session s;
        const HorizonSampler sampler(s.dry.heightfield(), s.dry.ecology(), horizonParams());
        Pool pool(2);
        auto map = mapCache(s);
        auto ground = groundRead(s);
        auto horizon = horizonRunner(s, sampler, pool);
        auto tiles = tileWorkers(s);
        std::this_thread::sleep_for(std::chrono::milliseconds(kStartMs));
        const Clock::time_point start = Clock::now();
        map.reset();
        ground.reset();
        horizon.reset();
        tiles.reset();
        runs.push_back({msSince(start), s.allStopped(), s.heavy.started->load(),
                        s.heavy.stopped->load()});
    }
    return runs;
}

// The ground reads are real ones (no Heavy in them): the time of one alone, then a read
// destroyed in the middle, then a read forgotten in the middle (a teleport asking another place):
// it stops at once, never waited for, and the next read gives the ground of a read on this
// thread.
void testGroundReads() {
    const map_world::FallPlace fall = map_world::firstFall();
    double alone = 0.0;
    {
        const Session s;
        const Clock::time_point start = Clock::now();
        (void)groundLevelAt(s.world.tiles, fall.x, fall.z);
        alone = msSince(start);
    }
    std::vector<double> destroyed;
    std::vector<double> forgotten;
    for (int k = 0; k < kTries; ++k) {
        {
            const Session s;
            auto read = groundRead(s);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            const Clock::time_point start = Clock::now();
            read.reset();
            destroyed.push_back(msSince(start));
        }
        const Session s;
        GroundRead read;
        read.ask(s.world.tiles, fall.x, fall.z);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        const Clock::time_point start = Clock::now();
        read.forget();
        while (read.running() > 0) {
            std::this_thread::yield();
        }
        forgotten.push_back(msSince(start));
        read.ask(s.world.tiles, fall.x, fall.z);
        std::optional<GroundLevel> level;
        while (!level.has_value()) {
            level = read.poll();
        }
        const GroundLevel here = groundLevelAt(s.world.tiles, fall.x, fall.z);
        TEST_CHECK(level->ground == here.ground && level->stand == here.stand);
    }
    for (std::vector<double>* ms : {&destroyed, &forgotten}) {
        std::sort(ms->begin(), ms->end());
        std::printf("the ground read %s in %.2f ms (median), %.2f ms at most, alone %.0f ms\n",
                    ms == &destroyed ? "destroyed" : "forgotten, out", (*ms)[ms->size() / 2],
                    ms->back(), alone);
        TEST_CHECK((*ms)[ms->size() / 2] <= 100.0);
        TEST_CHECK(ms->back() < 0.25 * alone);
    }
}

} // namespace

int main() {
    const double work = workMs();
    check("the cache of the dev map", stops([](const Session& s) { return mapCache(s); }), work);
    check("the workers of the tiles", stops([](const Session& s) { return tileWorkers(s); }), work);
    {
        std::vector<Run> runs;
        for (int k = 0; k < kTries; ++k) {
            const Session s;
            const HorizonSampler sampler(s.dry.heightfield(), s.dry.ecology(), horizonParams());
            Pool pool(2);
            auto runner = horizonRunner(s, sampler, pool);
            std::this_thread::sleep_for(std::chrono::milliseconds(kStartMs));
            const Clock::time_point start = Clock::now();
            runner.reset();
            runs.push_back({msSince(start), s.allStopped(), s.heavy.started->load(),
                        s.heavy.stopped->load()});
        }
        check("the GPU horizon", runs, work);
    }
    testGroundReads();
    check("all four, as ReleaseWorld", releaseWorld(), work);
    return test::result();
}

#pragma once

// Pure C++ layer of the Unreal plugin (no Unreal header): compiled by Unreal with the plugin and
// by CMake for its tests (tests/unreal_map_*_test.cpp).
//
// The tiles of the dev map and of the minimap (agent 35), rendered on threads of their own, never
// on the game thread, and kept: moving a view only renders the tiles it did not show yet.

#include "map_landing.hpp"

#include "terrain/map/map_frame.hpp"
#include "terrain/map/map_probe.hpp"
#include "terrain/map/map_render.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace terrain {
class MapWorldSource;
}

namespace terrain::unreal {

// A rendered tile: its key, the layers it shows (their revision), its image.
struct MapTile {
    MapTileKey key;
    std::uint64_t revision = 0;
    MapImage image;
    double seconds = 0.0; // to render it
};

struct MapCacheParams {
    // Tiles kept, each 256 KB (192: 48 MB).
    std::size_t capacity = 192;
    // Threads rendering the tiles, each one tile at a time.
    int threads = 1;
};

// The tiles of the map: a view asks for the tiles it shows (want), threads of the cache render
// those it does not hold, the thread owning the cache takes the new ones (takeReady) and reads
// them (find). Every method is thread-safe; the renders run outside the lock.
class MapTileCache {
public:
    // The image of a tile with these layers (renderMap over a source), called on the threads of the
    // cache, several at once; it stops soon (between two rows) once the flag is raised, throwing
    // MapCancelled, and the tile is dropped.
    using Render =
        std::function<MapImage(const MapTileKey&, const std::vector<std::string>&, MapCancel)>;
    // What the map says of a point (probeAt), called on the threads of the cache.
    using Probe = std::function<MapProbe(double x, double z)>;
    // Where a click at (x, z) puts the player, its dry ground looked for within radius meters
    // (clickLanding), called on the threads of the cache.
    using Land = std::function<MapLanding(double x, double z, double radius)>;

    MapTileCache(Render render, Probe probe, std::vector<std::string> layers,
                 const MapCacheParams& params = {}, Land land = {});
    // Stops the threads: the tiles being rendered stop between two of their rows and are dropped
    // (well under 100 ms), the others never start.
    ~MapTileCache();
    MapTileCache(const MapTileCache&) = delete;
    MapTileCache& operator=(const MapTileCache&) = delete;

    // The tiles the views show now, the most urgent first: those the cache does not hold are
    // rendered in this order; the tiles asked for before and not now are no longer rendered.
    void want(const std::vector<MapTileKey>& keys);
    // The tile of this key with the current layers, or null (marked as just used).
    [[nodiscard]] std::shared_ptr<const MapTile> find(const MapTileKey& key);
    // The keys of the tiles rendered since the last call, in the order they were done.
    [[nodiscard]] std::vector<MapTileKey> takeReady();

    // The layers drawn from now on: every tile kept is dropped (a new revision) and rendered again
    // when a view asks for it.
    void setLayers(std::vector<std::string> layers);
    [[nodiscard]] std::vector<std::string> layers() const;
    [[nodiscard]] std::uint64_t revision() const;

    // A probe of (x, z) on the threads of the cache: only the last asked is done.
    void probe(double x, double z);
    [[nodiscard]] std::optional<MapProbe> takeProbe();
    // The landing of a click on the threads of the cache, before the probe and the tiles (it may
    // read a few thousand columns of the core): only the last asked is done.
    void land(double x, double z, double radius);
    [[nodiscard]] std::optional<MapLanding> takeLanding();

    // The message of the last render or probe that threw (the tile is not kept), empty if none.
    [[nodiscard]] std::string lastError() const;
    // Counters: tiles rendered (whatever became of them), kept, waiting to be rendered.
    [[nodiscard]] std::size_t rendered() const;
    [[nodiscard]] std::size_t kept() const;
    [[nodiscard]] std::size_t waiting() const;
    // Until no tile waits nor is being rendered, and no probe nor landing waits.
    void waitIdle() const;

private:
    struct LandAsked {
        double x = 0.0;
        double z = 0.0;
        double radius = 0.0;
    };
    struct Job {
        std::optional<std::pair<double, double>> point; // a probe, else the tile below
        std::optional<LandAsked> landing;              // a landing, before all
        MapTileKey key;
        std::uint64_t revision = 0;
        std::vector<std::string> layers;
    };
    void loop();
    // The next job under the lock, false once the cache stops.
    bool nextJob(std::unique_lock<std::mutex>& lock, Job& job);
    // A landing or a probe to do now (no other thread doing one of its kind), under the lock.
    bool takePointJob(Job& job);
    void finish(const Job& job, std::optional<MapLanding> landed, std::optional<MapProbe> probed,
                std::optional<MapTile> tile, const std::string& error);
    void keep(MapTile tile);
    void evict();

    Render render_;
    Probe probe_;
    Land land_;
    MapCacheParams params_;
    mutable std::mutex mutex_;
    std::condition_variable work_;
    mutable std::condition_variable idle_;
    std::vector<std::string> layers_;
    std::uint64_t revision_ = 1;
    std::deque<MapTileKey> queue_;                  // wanted, not held, not being rendered
    std::vector<MapTileKey> rendering_;             // being rendered now
    std::map<MapTileKey, std::shared_ptr<const MapTile>> tiles_;
    std::map<MapTileKey, std::uint64_t> used_;      // the clock of the last use of each tile
    std::uint64_t clock_ = 0;
    std::vector<MapTileKey> ready_;
    std::optional<std::pair<double, double>> probeAsked_;
    std::optional<MapProbe> probeDone_;
    bool probing_ = false;
    std::optional<LandAsked> landAsked_;
    std::optional<MapLanding> landDone_;
    bool landing_ = false;
    std::size_t rendered_ = 0;
    std::string lastError_;
    bool stopping_ = false;
    std::atomic<bool> cancel_{false}; // raised when the cache stops: the renders in flight end
    std::vector<std::thread> threads_; // joined by the destructor
};

// The render, the probe and the landings of a world source (which must outlive the cache):
// renderMap of the default layers registry (map_style.hpp) on one thread per tile, probeAt, and
// clickLandingOn its heightfield.
[[nodiscard]] MapTileCache::Render renderTilesOf(const MapWorldSource& source);
[[nodiscard]] MapTileCache::Probe probesOf(const MapWorldSource& source);
[[nodiscard]] MapTileCache::Land landingsOf(const MapWorldSource& source);

} // namespace terrain::unreal

#include "map_tile_cache.hpp"

#include "terrain/generation/heightfield.hpp"
#include "terrain/map/map_source.hpp"
#include "terrain/map/map_style.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <set>
#include <stdexcept>
#include <utility>

namespace terrain::unreal {

MapTileCache::MapTileCache(Render render, Probe probe, std::vector<std::string> layers,
                           const MapCacheParams& params)
    : render_(std::move(render)), probe_(std::move(probe)), params_(params),
      layers_(std::move(layers)) {
    if (!render_ || params.capacity == 0 || params.threads < 1) {
        throw std::invalid_argument("map tile cache: a render, a capacity and a thread expected");
    }
    for (int i = 0; i < params.threads; ++i) {
        threads_.emplace_back([this] { loop(); });
    }
}

MapTileCache::~MapTileCache() {
    cancel_.store(true, std::memory_order_relaxed);
    {
        const std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    work_.notify_all();
    for (std::thread& t : threads_) {
        t.join();
    }
}

void MapTileCache::want(const std::vector<MapTileKey>& keys) {
    {
        const std::lock_guard lock(mutex_);
        queue_.clear();
        std::set<MapTileKey> seen;
        for (const MapTileKey& key : keys) {
            const bool busy =
                std::find(rendering_.begin(), rendering_.end(), key) != rendering_.end();
            if (tiles_.count(key) == 0 && !busy && seen.insert(key).second) {
                queue_.push_back(key);
            }
        }
    }
    work_.notify_all();
}

std::shared_ptr<const MapTile> MapTileCache::find(const MapTileKey& key) {
    const std::lock_guard lock(mutex_);
    const auto it = tiles_.find(key);
    if (it == tiles_.end()) {
        return nullptr;
    }
    used_[key] = ++clock_;
    return it->second;
}

std::vector<MapTileKey> MapTileCache::takeReady() {
    const std::lock_guard lock(mutex_);
    return std::exchange(ready_, {});
}

void MapTileCache::setLayers(std::vector<std::string> layers) {
    const std::lock_guard lock(mutex_);
    layers_ = std::move(layers);
    ++revision_;
    tiles_.clear();
    used_.clear();
    ready_.clear();
    queue_.clear();
}

std::vector<std::string> MapTileCache::layers() const {
    const std::lock_guard lock(mutex_);
    return layers_;
}

std::uint64_t MapTileCache::revision() const {
    const std::lock_guard lock(mutex_);
    return revision_;
}

void MapTileCache::probe(double x, double z) {
    if (!probe_) {
        return;
    }
    {
        const std::lock_guard lock(mutex_);
        probeAsked_ = std::pair{x, z};
    }
    work_.notify_one();
}

std::optional<MapProbe> MapTileCache::takeProbe() {
    const std::lock_guard lock(mutex_);
    return std::exchange(probeDone_, std::nullopt);
}

std::string MapTileCache::lastError() const {
    const std::lock_guard lock(mutex_);
    return lastError_;
}

std::size_t MapTileCache::rendered() const {
    const std::lock_guard lock(mutex_);
    return rendered_;
}

std::size_t MapTileCache::kept() const {
    const std::lock_guard lock(mutex_);
    return tiles_.size();
}

std::size_t MapTileCache::waiting() const {
    const std::lock_guard lock(mutex_);
    return queue_.size() + rendering_.size();
}

void MapTileCache::waitIdle() const {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] {
        return queue_.empty() && rendering_.empty() && !probeAsked_ && !probing_;
    });
}

bool MapTileCache::nextJob(std::unique_lock<std::mutex>& lock, Job& job) {
    work_.wait(lock, [this] { return stopping_ || probeAsked_ || !queue_.empty(); });
    if (stopping_) {
        return false;
    }
    if (probeAsked_ && !probing_) {
        job.point = std::exchange(probeAsked_, std::nullopt);
        probing_ = true;
        return true;
    }
    if (queue_.empty()) { // another thread probes: wait for it
        work_.wait(lock, [this] { return stopping_ || !queue_.empty() || !probing_; });
        return nextJob(lock, job);
    }
    job.point.reset();
    job.key = queue_.front();
    queue_.pop_front();
    rendering_.push_back(job.key);
    job.revision = revision_;
    job.layers = layers_;
    return true;
}

void MapTileCache::loop() {
    std::unique_lock lock(mutex_);
    Job job;
    while (nextJob(lock, job)) {
        lock.unlock();
        std::optional<MapProbe> probed;
        std::optional<MapTile> tile;
        std::string error;
        try {
            if (job.point) {
                probed = probe_(job.point->first, job.point->second);
            } else {
                const auto start = std::chrono::steady_clock::now();
                MapImage image = render_(job.key, job.layers, &cancel_);
                const std::chrono::duration<double> spent = std::chrono::steady_clock::now() - start;
                tile = MapTile{job.key, job.revision, std::move(image), spent.count()};
            }
        } catch (const MapCancelled&) {
            // the cache stops: the tile is dropped, nothing went wrong
        } catch (const std::exception& e) {
            error = e.what();
        } catch (...) {
            error = "unknown error";
        }
        lock.lock();
        if (job.point) {
            probing_ = false;
            probeDone_ = probed ? probed : probeDone_;
        } else {
            rendering_.erase(std::find(rendering_.begin(), rendering_.end(), job.key));
            ++rendered_;
            if (tile && tile->revision == revision_) {
                keep(std::move(*tile));
            }
        }
        lastError_ = error.empty() ? lastError_ : error;
        idle_.notify_all();
        work_.notify_all();
    }
}

void MapTileCache::keep(MapTile tile) {
    const MapTileKey key = tile.key;
    tiles_[key] = std::make_shared<const MapTile>(std::move(tile));
    used_[key] = ++clock_;
    ready_.push_back(key);
    evict();
}

// Beyond the capacity, the tiles used the longest ago go (the views mark those they draw through
// find, every frame).
void MapTileCache::evict() {
    while (tiles_.size() > params_.capacity) {
        auto oldest = used_.end();
        for (auto it = used_.begin(); it != used_.end(); ++it) {
            if (oldest == used_.end() || it->second < oldest->second) {
                oldest = it;
            }
        }
        tiles_.erase(oldest->first);
        ready_.erase(std::remove(ready_.begin(), ready_.end(), oldest->first), ready_.end());
        used_.erase(oldest);
    }
}

MapTileCache::Render renderTilesOf(const MapWorldSource& source) {
    auto layers = std::make_shared<const MapLayers>(defaultMapLayers());
    return [&source, layers](const MapTileKey& key, const std::vector<std::string>& enabled,
                             MapCancel cancel) {
        return renderMap(source, mapTileFrame(key, source.field().world()), *layers, enabled,
                         {1, kMapMaxSamples, cancel});
    };
}

MapTileCache::Probe probesOf(const MapWorldSource& source) {
    return [&source](double x, double z) { return probeAt(source, x, z); };
}

} // namespace terrain::unreal

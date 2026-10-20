#include "terrain/streaming/stream_manager.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace terrain {

namespace {

// Ticket of a loaded tile whose data an invalidation made stale.
constexpr std::uint64_t kStale = 0;

bool overlapsAny(const TileKey& tile, std::span<const ChunkCoord> chunks) {
    return std::any_of(chunks.begin(), chunks.end(),
                       [&tile](ChunkCoord chunk) { return tileContains(tile, chunk); });
}

// Unloading order, for deterministic changes.
bool keyOrder(const TileKey& a, const TileKey& b) {
    return std::tuple(a.lod, a.origin.z, a.origin.x) < std::tuple(b.lod, b.origin.z, b.origin.x);
}

} // namespace

StreamManager::StreamManager(TileGenerator generator, const StreamParams& params)
    : generator_(std::move(generator)), params_(params) {
    validate(params_.lod);
    validate(params_.world);
    if (params_.workerThreads < 0 || params_.maxLoadsPerUpdate == 0) {
        throw std::invalid_argument("StreamParams: invalid thread count or loads per update");
    }
    const ChunkCoord count = worldChunks(params_.world);
    const int span = 2 * (params_.lod.viewDistance + tileChunks(kMaxLod)) + tileChunks(kMaxLod);
    if (count.x < span || count.z < span) {
        throw std::invalid_argument("StreamParams: the world is too small for the view distance");
    }
    for (int i = 0; i < params_.workerThreads; ++i) {
        workers_.emplace_back([this] { workerLoop(); });
    }
}

StreamManager::~StreamManager() {
    {
        const std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    workAvailable_.notify_all();
    workers_.clear();
}

StreamChanges StreamManager::update(double playerX, double playerZ) {
    rethrowError();
    const ChunkCoord player = wrapChunk(worldToChunk(playerX, playerZ), params_.world);
    if (!player_ || *player_ != player) {
        reselect(player);
    }
    if (queueDirty_) {
        rebuildQueue();
    } else {
        takeResults();
    }
    StreamChanges changes;
    integrate(changes);
    unloadCovered(changes);
    return changes;
}

std::size_t StreamManager::generatePending(std::size_t maxTiles) {
    if (queueDirty_) {
        rebuildQueue();
    }
    std::unique_lock lock(mutex_);
    std::size_t generated = 0;
    while (generated < maxTiles && !queue_.empty()) {
        runJob(lock);
        ++generated;
    }
    return generated;
}

void StreamManager::waitIdle() {
    if (workers_.empty()) {
        generatePending(std::numeric_limits<std::size_t>::max());
    } else {
        if (queueDirty_) {
            rebuildQueue();
        }
        std::unique_lock lock(mutex_);
        workDone_.wait(lock, [this] {
            return error_ != nullptr || (queue_.empty() && inFlight_.empty());
        });
    }
    rethrowError();
}

void StreamManager::invalidate(std::span<const ChunkCoord> chunks) {
    std::vector<ChunkCoord> canonical;
    canonical.reserve(chunks.size());
    for (const ChunkCoord chunk : chunks) {
        canonical.push_back(wrapChunk(chunk, params_.world));
    }
    for (auto& [key, wanted] : wanted_) {
        if (overlapsAny(key, canonical)) {
            wanted.ticket = nextTicket_++;
        }
    }
    for (auto& [key, loaded] : loaded_) {
        if (overlapsAny(key, canonical)) {
            loaded.ticket = kStale;
        }
    }
    rebuildQueue();
}

const TileData* StreamManager::find(const TileKey& key) const {
    const auto it = loaded_.find(wrapTile(key, params_.world));
    return it == loaded_.end() ? nullptr : it->second.data.get();
}

std::vector<TileKey> StreamManager::loadedTiles() const {
    std::vector<TileKey> keys;
    keys.reserve(loaded_.size());
    for (const auto& [key, loaded] : loaded_) {
        keys.push_back(key);
    }
    const ChunkCoord player = player_.value_or(ChunkCoord{});
    std::sort(keys.begin(), keys.end(),
              [this, player](const TileKey& a, const TileKey& b) { return nearer(a, b, player); });
    return keys;
}

std::size_t StreamManager::pendingCount() const {
    std::size_t pending = 0;
    for (const auto& [key, wanted] : wanted_) {
        pending += current(key, wanted.ticket) ? 0 : 1;
    }
    return pending;
}

// True if the loaded data of the tile has this ticket.
bool StreamManager::current(const TileKey& key, std::uint64_t ticket) const {
    const auto it = loaded_.find(key);
    return it != loaded_.end() && it->second.ticket == ticket;
}

bool StreamManager::nearer(const TileKey& a, const TileKey& b, ChunkCoord player) const {
    return loadsBefore(nearestTile(a, player, params_.world), nearestTile(b, player, params_.world),
                       player);
}

// A tile keeps its ticket while its request is unchanged, so it is not generated again; a tile
// coming back into the selection reuses its loaded data when it is still valid. The selection
// and its seams are computed around the canonical player in unwrapped coordinates, then the keys
// are made canonical: the world is wide enough for them to stay distinct.
void StreamManager::reselect(ChunkCoord player) {
    player_ = player;
    selection_ = tileRequests(selectTiles(player, params_.lod));
    for (TileRequest& request : selection_) {
        request.key = wrapTile(request.key, params_.world);
    }
    std::unordered_map<TileKey, Wanted, TileKeyHash> wanted;
    wanted.reserve(selection_.size());
    for (const TileRequest& request : selection_) {
        std::uint64_t ticket = 0;
        const auto previous = wanted_.find(request.key);
        const auto loaded = loaded_.find(request.key);
        if (previous != wanted_.end() && previous->second.request == request) {
            ticket = previous->second.ticket;
        } else if (loaded != loaded_.end() && loaded->second.ticket != kStale &&
                   loaded->second.data->request == request) {
            ticket = loaded->second.ticket;
        } else {
            ticket = nextTicket_++;
        }
        wanted.emplace(request.key, Wanted{request, ticket});
    }
    wanted_ = std::move(wanted);
    queueDirty_ = true;
}

void StreamManager::takeResults() {
    const std::lock_guard lock(mutex_);
    takeResultsLocked();
}

void StreamManager::takeResultsLocked() {
    std::move(results_.begin(), results_.end(), std::back_inserter(ready_));
    results_.clear();
}

// Queues, nearest first, every selected tile whose current ticket is neither loaded, nor
// generated and waiting, nor being generated. Under one lock, so that no tile finishes unseen
// between the results and the tiles in flight.
void StreamManager::rebuildQueue() {
    const std::lock_guard lock(mutex_);
    takeResultsLocked();
    std::unordered_set<std::uint64_t> generated;
    for (const Result& result : ready_) {
        generated.insert(result.ticket);
    }
    queue_.clear();
    for (auto it = selection_.rbegin(); it != selection_.rend(); ++it) {
        const std::uint64_t ticket = wanted_.at(it->key).ticket;
        if (!current(it->key, ticket) && !generated.contains(ticket) &&
            !inFlight_.contains(ticket)) {
            queue_.push_back({*it, ticket});
        }
    }
    queueDirty_ = false;
    workAvailable_.notify_all();
}

// Loads up to maxLoadsPerUpdate generated tiles, nearest first; stale ones are dropped.
void StreamManager::integrate(StreamChanges& changes) {
    const ChunkCoord player = player_.value_or(ChunkCoord{});
    std::sort(ready_.begin(), ready_.end(), [this, player](const Result& a, const Result& b) {
        return nearer(a.key, b.key, player) || (a.key == b.key && a.ticket < b.ticket);
    });
    std::vector<Result> waiting;
    for (Result& result : ready_) {
        const auto wanted = wanted_.find(result.key);
        if (wanted == wanted_.end() || wanted->second.ticket != result.ticket ||
            current(result.key, result.ticket)) {
            continue; // stale, or already loaded
        }
        if (changes.loaded.size() == params_.maxLoadsPerUpdate) {
            waiting.push_back(std::move(result));
            continue;
        }
        loaded_[result.key] = {result.ticket,
                               std::make_shared<const TileData>(std::move(result.data))};
        changes.loaded.push_back(result.key);
    }
    ready_ = std::move(waiting);
}

// Unloads the tiles that left the selection once the selected tiles overlapping them are loaded.
void StreamManager::unloadCovered(StreamChanges& changes) {
    std::vector<TileKey> unloaded;
    for (const auto& [key, loaded] : loaded_) {
        if (!wanted_.contains(key) && covered(key)) {
            unloaded.push_back(key);
        }
    }
    std::sort(unloaded.begin(), unloaded.end(), keyOrder);
    for (const TileKey& key : unloaded) {
        loaded_.erase(key);
        changes.unloaded.push_back(key);
    }
}

// True if every selected tile overlapping the tile is loaded: its ancestors and its descendants.
bool StreamManager::covered(const TileKey& retiring) const {
    const auto missing = [this](const TileKey& key) {
        return wanted_.contains(key) && !loaded_.contains(key);
    };
    for (int lod = retiring.lod; lod <= kMaxLod; ++lod) {
        if (missing(tileContaining(retiring.origin, lod))) {
            return false;
        }
    }
    for (int lod = 0; lod < retiring.lod; ++lod) {
        const int step = tileChunks(lod);
        const int span = tileChunks(retiring.lod);
        for (int dz = 0; dz < span; dz += step) {
            for (int dx = 0; dx < span; dx += step) {
                const ChunkCoord origin{retiring.origin.x + dx, retiring.origin.z + dz};
                if (missing({origin, lod})) {
                    return false;
                }
            }
        }
    }
    return true;
}

// Generates the job at the back of the queue with the lock released.
void StreamManager::runJob(std::unique_lock<std::mutex>& lock) {
    Job job = std::move(queue_.back());
    queue_.pop_back();
    inFlight_.insert(job.ticket);
    lock.unlock();
    std::optional<TileData> data;
    std::exception_ptr error;
    try {
        data = generator_(job.request);
    } catch (...) {
        error = std::current_exception();
    }
    lock.lock();
    inFlight_.erase(job.ticket);
    if (data) {
        results_.push_back({job.request.key, job.ticket, std::move(*data)});
    } else if (error_ == nullptr) {
        error_ = error;
    }
    workDone_.notify_all();
}

void StreamManager::workerLoop() {
    std::unique_lock lock(mutex_);
    while (true) {
        workAvailable_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
        if (stopping_) {
            return;
        }
        runJob(lock);
    }
}

void StreamManager::rethrowError() {
    std::exception_ptr error;
    {
        const std::lock_guard lock(mutex_);
        std::swap(error, error_);
    }
    if (error != nullptr) {
        queueDirty_ = true; // the failed tile is queued again by the next update
        std::rethrow_exception(error);
    }
}

} // namespace terrain

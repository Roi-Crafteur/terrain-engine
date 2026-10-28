#pragma once

#include "terrain/streaming/horizon_edits.hpp"
#include "terrain/streaming/horizon_epoch.hpp"

#include <condition_variable>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace terrain {

struct HorizonStreamParams {
    // Threads computing the pieces of the epochs. 0: pieces are only computed by
    // generatePending and waitIdle, on the calling thread (synchronous, for tests).
    int workerThreads = 2;
    std::size_t pieceSamples = kHorizonPieceSamples;
};

// The far horizon around an observer, computed by worker threads: the reference driver of a
// HorizonClipmap, for the core, its tests and its tools. A renderer with its own task system
// drives the clipmap itself (plan, computePiece on its threads, commit) instead.
//
// One epoch at a time: an update commits the epoch in flight once every piece is computed (all
// the levels move at once), then plans the next one around the last position of the observer.
// The workers take the pieces of the finest levels first. Once idle, the samples only depend on
// the last position, the sampler and the invalidations, never on the threads.
//
// Every method must be called from the thread owning the streamer; only computePiece runs on the
// workers. An exception thrown while a piece is computed is rethrown by the next update or
// waitIdle: the epoch is dropped (its invalidations are kept) and the next update plans again.
//
// With the map of edits the sampler reads (HorizonEditMap::source), the edits staged in it take
// effect between two epochs, in the order they were staged: an update with no epoch in flight
// applies them and invalidates their areas before it plans the next one, so that the epochs only
// compute again the samples they change and every epoch reads one state of the edits.
class HorizonStreamer {
public:
    // The sampler, and the map of edits it reads if any, must outlive the streamer. Throws
    // std::invalid_argument for a negative thread count, pieceSamples 0 or a map the sampler
    // cannot read (HorizonEditMap::validate).
    explicit HorizonStreamer(const HorizonSampler& sampler, const HorizonStreamParams& params = {},
                             HorizonEditMap* edits = nullptr);
    ~HorizonStreamer(); // stops the workers once their current piece is computed

    HorizonStreamer(const HorizonStreamer&) = delete;
    HorizonStreamer& operator=(const HorizonStreamer&) = delete;

    // Commits the epoch in flight if it is computed, then, if none is in flight, applies the
    // staged edits and plans the next one if the levels must move or be computed again. Returns
    // what the commit changed.
    HorizonChanges update(double x, double z);

    // Computes up to maxPieces queued pieces, finest levels first, on the calling thread.
    // Returns how many were computed; the next update commits the epoch once complete.
    std::size_t generatePending(std::size_t maxPieces);

    // Waits until every piece of the epoch in flight is computed: with no worker, computes them
    // on the calling thread.
    void waitIdle();

    // The samples around an area whose edit summaries changed are computed again (see
    // HorizonClipmap::invalidate).
    void invalidate(double minX, double minZ, double maxX, double maxZ);

    // True while an epoch is planned and not committed.
    [[nodiscard]] bool busy() const { return epoch_ != nullptr; }
    [[nodiscard]] const HorizonClipmap& clipmap() const { return clipmap_; }
    [[nodiscard]] const HorizonLevel& level(int level) const { return clipmap_.level(level); }
    [[nodiscard]] const HorizonStreamParams& params() const { return params_; }

private:
    void startEpoch(HorizonEpoch epoch);
    void commitIfComplete(HorizonChanges& changes);
    void dropEpoch();
    void runJob(std::unique_lock<std::mutex>& lock);
    void workerLoop();
    void rethrowError();

    const HorizonSampler& sampler_;
    HorizonStreamParams params_;
    HorizonEditMap* edits_;

    // Owner thread only. The workers write the cells of the pieces of the epoch, which lives
    // until none of them is queued or computed.
    HorizonClipmap clipmap_;
    std::unique_ptr<HorizonEpoch> epoch_;

    // Shared with the workers, guarded by mutex_.
    std::mutex mutex_;
    std::condition_variable workAvailable_;
    std::condition_variable workDone_;
    std::vector<HorizonPiece*> queue_; // the next piece at the back
    std::size_t remaining_ = 0;        // pieces of the epoch queued or being computed
    std::size_t inFlight_ = 0;
    std::exception_ptr error_;
    bool stopping_ = false;
    std::vector<std::jthread> workers_; // last member: joined before the others are destroyed
};

} // namespace terrain

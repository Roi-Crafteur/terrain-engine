#pragma once

#include "terrain/streaming/horizon_edits.hpp"
#include "terrain/streaming/horizon_epoch.hpp"
#include "terrain/streaming/horizon_levels.hpp"
#include "terrain/streaming/horizon_sampler.hpp"

#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace terrain::unreal {

// Runs a job later on some thread: a low priority task of Unreal (UE::Tasks), a thread pool or
// the calling thread in the tests. Called from the thread owning the runner and from the jobs
// themselves: it must be thread-safe.
using JobLauncher = std::function<void(std::function<void()>)>;

// Turns the cells of a computed piece into what its renderer uploads (the texels of the GPU
// horizon), on the thread that computed it. Thread-safe.
using PiecePreparer = std::function<std::vector<float>(const HorizonPiece&)>;

struct HorizonEpochParams {
    // Pieces computed at once: the horizon never takes more of the task threads.
    int maxPiecesInFlight = 2;
    std::size_t pieceSamples = kHorizonPieceSamples;
};

// A piece of a committed epoch with what the preparer made of it.
struct PreparedPiece {
    int level = 0;
    HorizonRect rect;
    std::vector<float> data;
};

// What a commit changed (HorizonClipmap::commit) and the prepared pieces it wrote, finest
// levels first.
struct EpochCommit {
    HorizonChanges changes;
    std::vector<PreparedPiece> pieces;

    [[nodiscard]] bool empty() const { return changes.empty(); }
};

// The far horizon driven through a task system (phase 13, step 2): the HorizonClipmap of
// terrain_core, whose pieces run as jobs of a JobLauncher, at most maxPiecesInFlight at once
// (each job launches the next piece when it ends), instead of the threads of HorizonStreamer.
// One epoch at a time; its levels move at once when it is committed. Every method must be called
// from the thread owning the runner; only the jobs run elsewhere. Once idle, the samples only
// depend on the last position and the sampler, as with HorizonStreamer.
//
// With the map of edits the sampler reads, an update with no epoch in flight applies the staged
// edits and invalidates their areas before it plans (as HorizonStreamer): the next epoch
// computes again the samples around them, and its commit sends only those bands.
class HorizonEpochRunner {
public:
    // The sampler, and the map of edits it reads if any, must outlive the runner. Throws
    // std::invalid_argument without a launcher, for maxPiecesInFlight below 1, pieceSamples 0 or
    // a map the sampler cannot read (HorizonEditMap::validate).
    HorizonEpochRunner(const HorizonSampler& sampler, JobLauncher launch,
                       PiecePreparer prepare = {}, const HorizonEpochParams& params = {},
                       HorizonEditMap* edits = nullptr);
    ~HorizonEpochRunner(); // waits for the jobs in flight

    HorizonEpochRunner(const HorizonEpochRunner&) = delete;
    HorizonEpochRunner& operator=(const HorizonEpochRunner&) = delete;

    // Commits the epoch in flight once every piece is computed, then, when none is in flight,
    // applies the staged edits and plans the next one around (x, z) and launches its first
    // pieces. Rethrows the error of a job: the epoch is dropped and planned again by the next
    // update.
    EpochCommit update(double x, double z);

    // Waits until every piece of the epoch in flight is computed (or an error).
    void waitIdle();

    [[nodiscard]] bool busy() const { return epoch_ != nullptr; }
    [[nodiscard]] const HorizonClipmap& clipmap() const { return clipmap_; }
    [[nodiscard]] const HorizonLevel& level(int level) const { return clipmap_.level(level); }

private:
    void start(HorizonEpoch epoch);
    void commitIfComplete(EpochCommit& result);
    void dropEpoch();
    void rethrowError();
    void runPiece(std::size_t index);
    [[nodiscard]] std::size_t takeNext(); // under the lock; pieces size when none is left

    const HorizonSampler& sampler_;
    JobLauncher launch_;
    PiecePreparer prepare_;
    HorizonEpochParams params_;
    HorizonEditMap* edits_;
    HorizonClipmap clipmap_; // owner thread only

    // The epoch in flight: the jobs write its pieces and their prepared data, each its own.
    std::unique_ptr<HorizonEpoch> epoch_;
    std::vector<std::vector<float>> prepared_;

    // Shared with the jobs, guarded by mutex_.
    std::mutex mutex_;
    std::condition_variable done_;
    std::size_t next_ = 0;      // next piece to launch
    std::size_t remaining_ = 0; // pieces not computed yet
    std::size_t inFlight_ = 0;
    std::exception_ptr error_;
    bool stopping_ = false;
};

} // namespace terrain::unreal

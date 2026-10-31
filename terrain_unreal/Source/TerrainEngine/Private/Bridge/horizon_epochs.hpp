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
    // Pieces computed at once: the horizon never takes more of the task threads
    // (setMaxPiecesInFlight changes it while running).
    int maxPiecesInFlight = 2;
    std::size_t pieceSamples = kHorizonPieceSamples;
    // A new place (the first update, or a jump out of the window of the finest level) is
    // brought level by level, finest first, each level shown as soon as its epoch is committed
    // (HorizonClipmap::planFinest), and the epoch in flight for the old place is abandoned
    // (agent 38). False: one epoch of every level, as before.
    bool progressive = false;
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
// Progressive (HorizonEpochParams::progressive): at a new place, levelsShown() falls to 0 (the
// windows of the old place no longer nest around the player), the epoch in flight is abandoned
// without waiting (no piece of it launched any more, forgotten once its pieces in flight end),
// then the epochs bring one more level each, finest first: levelsShown() counts the finest levels
// placed around the player, which a renderer shows, nested, the coarser ones hidden. Walking
// within the finest window moves every level at once, as before.
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

    // Waits until every piece of the epoch in flight is computed (or an error); for an abandoned
    // epoch, until its pieces in flight end.
    void waitIdle();

    // The pieces computed at once from now on (at least 1): more are launched at once when it
    // grows, fewer once the pieces in flight end when it shrinks.
    void setMaxPiecesInFlight(int pieces);
    [[nodiscard]] int maxPiecesInFlight() const;
    // The finest levels placed around the player (see above): every level once a place is
    // loaded, fewer while a new place is brought level by level, 0 before the first commit.
    [[nodiscard]] int levelsShown() const { return levelsShown_; }

    [[nodiscard]] bool busy() const { return epoch_ != nullptr; }
    [[nodiscard]] const HorizonClipmap& clipmap() const { return clipmap_; }
    [[nodiscard]] const HorizonLevel& level(int level) const { return clipmap_.level(level); }

private:
    void noteNewPlace(double x, double z);
    [[nodiscard]] bool settleAbandoned();
    void planNext(double x, double z);
    [[nodiscard]] bool near(double ax, double az, double bx, double bz) const;
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
    double epochX_ = 0.0; // where it was planned around
    double epochZ_ = 0.0;
    bool abandoned_ = false; // owner thread: never committed, forgotten once its jobs end
    int levelsShown_ = 0;    // owner thread

    // Shared with the jobs, guarded by mutex_.
    mutable std::mutex mutex_;
    std::condition_variable done_;
    std::size_t next_ = 0;      // next piece to launch
    std::size_t remaining_ = 0; // pieces not computed yet
    std::size_t inFlight_ = 0;
    int maxPieces_ = 1;
    std::exception_ptr error_;
    bool stopping_ = false;
};

} // namespace terrain::unreal

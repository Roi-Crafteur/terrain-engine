#pragma once

#include "horizon_epochs.hpp"
#include "horizon_normal_texels.hpp"

#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/horizon_epoch.hpp"
#include "terrain/streaming/horizon_normals.hpp"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <mutex>
#include <vector>

namespace terrain::unreal {

struct HorizonNormalParams {
    int ratio = 4;            // detail texels per sample along each axis (HorizonNormalSampler)
    int maxJobsInFlight = 2;  // the detail never takes more of the task threads
    std::int64_t jobSamples = 1024; // samples per job: a few tens of milliseconds in mountains
};

// The detail normals of the GPU horizon, computed after the samples. Each commit of the clipmap
// marks the texels of the samples it wrote pending (shown with the normals of the grid) and
// queues their detail; jobs compute it, finest levels first, at most maxJobsInFlight at once; the
// detail of the samples still in their windows is then written. The levels show as soon as
// their samples are ready, as without the detail, and gain their detail a little later: a few
// seconds for a whole new horizon, a few milliseconds per step of a walk.
//
// The detail of a sample is a pure function of the seed and its canonical index
// (HorizonNormalSampler): once idle, the texels only depend on the windows, never on the order of
// the jobs. Every method must be called from the thread owning the stream; only the jobs run
// elsewhere.
class HorizonNormalStream {
public:
    // field must outlive the stream and share the world of params. Throws
    // std::invalid_argument for invalid parameters, a ratio the sampler refuses, no launcher,
    // maxJobsInFlight below 1 or jobSamples below 1.
    HorizonNormalStream(const Heightfield& field, const HorizonParams& params,
                        const HorizonNormalParams& normals, JobLauncher launch);
    ~HorizonNormalStream(); // waits for the jobs in flight

    HorizonNormalStream(const HorizonNormalStream&) = delete;
    HorizonNormalStream& operator=(const HorizonNormalStream&) = delete;

    // After a commit of the clipmap: the pending texels of the samples it wrote (into out), and
    // their detail queued. The renderer creates its textures with every texel pending: the
    // first commit marks none.
    void written(const HorizonClipmap& clipmap, const HorizonChanges& changes,
                 std::vector<NormalRegion>& out);
    // The detail computed since the last call, for the samples still in their windows (into
    // out). Rethrows the error of a job.
    void collect(std::vector<NormalRegion>& out);
    // Waits until every queued job is done (or an error).
    void waitIdle();

    [[nodiscard]] bool busy();
    [[nodiscard]] int ratio() const { return sampler_.ratio(); }
    [[nodiscard]] int side() const { return side_; } // samples per side of a window
    [[nodiscard]] int textureSide() const { return side_ * sampler_.ratio(); }
    // Bytes of the detail textures of every level (two half floats per texel).
    [[nodiscard]] std::int64_t textureBytes() const;
    [[nodiscard]] std::int64_t samplesComputed() const { return samplesComputed_; }

private:
    struct Task {
        int level = 0;
        HorizonRect rect;
    };
    struct Result {
        int level = 0;
        HorizonRect rect;
        std::vector<std::uint16_t> texels;
    };

    void launchJobs(); // under no lock
    void runJob();
    [[nodiscard]] bool takeTask(Task& task); // under the lock

    HorizonNormalSampler sampler_;
    HorizonNormalParams params_;
    JobLauncher launch_;
    int side_;
    bool placed_ = false;              // owner thread: a commit was written
    std::int64_t samplesComputed_ = 0; // owner thread

    // Shared with the jobs, guarded by mutex_.
    std::mutex mutex_;
    std::condition_variable done_;
    std::vector<HorizonRect> windows_; // of every level, after the last commit
    std::deque<Task> queue_;
    std::vector<Result> results_;
    int inFlight_ = 0;
    std::exception_ptr error_;
    bool stopping_ = false;
};

// The texels of the samples `part` (inside rect) out of the detail texels of rect, laid out as
// HorizonNormalSampler::fill lays them out.
[[nodiscard]] std::vector<std::uint16_t> partTexels(const HorizonRect& rect, int ratio,
                                                    std::span<const std::uint16_t> texels,
                                                    const HorizonRect& part);

} // namespace terrain::unreal

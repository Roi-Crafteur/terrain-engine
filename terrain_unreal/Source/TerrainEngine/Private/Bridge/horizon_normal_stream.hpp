#pragma once

#include "horizon_crest_texels.hpp"
#include "horizon_epochs.hpp"
#include "horizon_normal_texels.hpp"

#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/horizon_detail.hpp"
#include "terrain/streaming/horizon_epoch.hpp"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <mutex>
#include <vector>

namespace terrain::unreal {

struct HorizonNormalParams {
    // Detail texels per sample along each axis (HorizonNormalSampler), 0 for no detail normal.
    int ratio = 4;
    int maxJobsInFlight = 2;  // the detail never takes more of the task threads
    std::int64_t jobSamples = 1024; // samples per job: a few tens of milliseconds in mountains
};

// The detail of the GPU horizon, computed after the samples: its detail normals and, when the
// crests are deferred (HorizonParams::crestRatio of the stream, 0 for none), the crests of its far
// mountains, both from one fine grid per job (HorizonDetailSampler). Each commit of the clipmap
// marks the normals of the samples it wrote pending (shown with the normals of the grid) and
// queues their detail in tiles of about jobSamples samples, as square as their rectangle allows;
// jobs compute it, finest levels first, at most maxJobsInFlight at once; the detail of the samples
// still in their windows is then written: the normals, and the crests as bands (CrestBand,
// horizon_crest_texels.hpp). The levels show as soon as their samples are ready, as without the
// detail, and gain their detail a little later: a few seconds for a whole new horizon, a few
// milliseconds per step of a walk.
//
// The detail of a sample is a pure function of the seed and its canonical index: once idle, the
// texels only depend on the windows, never on the order of the jobs. Every method must be called
// from the thread owning the stream; only the jobs run elsewhere.
class HorizonNormalStream {
public:
    // field must outlive the stream and share the world of params, whose crestRatio is that of
    // the deferred crests (0: none). Throws std::invalid_argument for invalid parameters, a ratio
    // HorizonNormalSampler refuses (but 0), no launcher, maxJobsInFlight below 1 or jobSamples
    // below 1.
    HorizonNormalStream(const Heightfield& field, const HorizonParams& params,
                        const HorizonNormalParams& normals, JobLauncher launch);
    ~HorizonNormalStream(); // waits for the jobs in flight

    HorizonNormalStream(const HorizonNormalStream&) = delete;
    HorizonNormalStream& operator=(const HorizonNormalStream&) = delete;

    // After a commit of the clipmap: the pending normals of the samples it wrote (into out), and
    // their detail queued. The renderer creates its textures with every texel pending: the
    // first commit marks none.
    void written(const HorizonClipmap& clipmap, const HorizonChanges& changes,
                 std::vector<NormalRegion>& out);
    // The detail computed since the last call, for the samples still in their windows: the
    // normals (into normals) and the crests (into crests, their bands clipped to the windows).
    // Rethrows the error of a job.
    void collect(std::vector<NormalRegion>& normals, std::vector<CrestBand>& crests);
    // Waits until every queued job is done (or an error).
    void waitIdle();

    // Whether a job is queued or running, or its detail not collected yet.
    [[nodiscard]] bool busy();
    [[nodiscard]] int ratio() const { return sampler_.normalRatio(); } // 0 without normals
    [[nodiscard]] int crestRatio() const { return sampler_.params().crestRatio; }
    [[nodiscard]] int side() const { return side_; } // samples per side of a window
    [[nodiscard]] int textureSide() const { return side_ * ratio(); }
    // Bytes of the detail textures of every level (two half floats per texel), 0 without normals.
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
        std::vector<std::uint16_t> texels; // normals, empty without
        std::vector<float> points;         // crests, empty where the level does not raise
        std::vector<float> raised;
    };

    void launchJobs(); // under no lock
    void runJob();
    [[nodiscard]] bool takeTask(Task& task); // under the lock
    [[nodiscard]] Result compute(const Task& task) const;
    void collectResult(const Result& result, const HorizonRect& inside,
                       std::vector<NormalRegion>& normals, std::vector<CrestBand>& crests) const;

    HorizonDetailSampler sampler_;
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

// A rectangle cut into tiles of about `samples` samples each, as square as it allows (whole rows
// of a band narrower than a tile), row of tiles by row of tiles: they cover it once.
[[nodiscard]] std::vector<HorizonRect> detailTiles(const HorizonRect& rect, std::int64_t samples);

} // namespace terrain::unreal

#include "horizon_normal_stream.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace terrain::unreal {

HorizonNormalStream::HorizonNormalStream(const Heightfield& field, const HorizonParams& params,
                                         const HorizonNormalParams& normals, JobLauncher launch)
    : sampler_(field, params, normals.ratio), params_(normals), launch_(std::move(launch)),
      side_(samplesPerSide(params)), windows_(static_cast<std::size_t>(params.levels)) {
    if (!launch_ || params_.maxJobsInFlight < 1 || params_.jobSamples < 1) {
        throw std::invalid_argument("HorizonNormalStream: a launcher, at least one job in flight "
                                    "and jobs of at least one sample expected");
    }
}

HorizonNormalStream::~HorizonNormalStream() {
    std::unique_lock lock(mutex_);
    stopping_ = true;
    queue_.clear();
    done_.wait(lock, [this] { return inFlight_ == 0; });
}

std::int64_t HorizonNormalStream::textureBytes() const {
    const std::int64_t texels = static_cast<std::int64_t>(textureSide()) * textureSide();
    return texels * kNormalHalves * 2 * sampler_.params().levels;
}

void HorizonNormalStream::written(const HorizonClipmap& clipmap, const HorizonChanges& changes,
                                  std::vector<NormalRegion>& out) {
    std::vector<Task> tasks;
    for (const HorizonLevelChange& change : changes.levels) {
        for (const HorizonRect& rect : change.written) {
            // The textures are created pending: nothing to mark before the first commit.
            for (NormalRegion& region :
                 placed_ ? pendingRegions(change.level, side_, ratio(), rect)
                         : std::vector<NormalRegion>{}) {
                out.push_back(std::move(region));
            }
            const std::int64_t rows = std::max<std::int64_t>(1, params_.jobSamples / rect.width());
            for (std::int64_t z = rect.z0; z < rect.z1; z += rows) {
                tasks.push_back({change.level, {rect.x0, z, rect.x1, std::min(z + rows, rect.z1)}});
            }
        }
    }
    {
        const std::lock_guard lock(mutex_);
        for (int level = 0; level < clipmap.levelCount(); ++level) {
            windows_[static_cast<std::size_t>(level)] = clipmap.level(level).window();
        }
        queue_.insert(queue_.end(), tasks.begin(), tasks.end());
    }
    placed_ = true;
    launchJobs();
}

void HorizonNormalStream::launchJobs() {
    int launches = 0;
    {
        const std::lock_guard lock(mutex_);
        while (inFlight_ + launches < params_.maxJobsInFlight &&
               static_cast<std::size_t>(launches) < queue_.size() && error_ == nullptr &&
               !stopping_) {
            ++launches;
        }
        inFlight_ += launches;
    }
    for (int n = 0; n < launches; ++n) {
        launch_([this] { runJob(); });
    }
}

// The next task still in its window, clipped to it; false when none is left.
bool HorizonNormalStream::takeTask(Task& task) {
    while (!queue_.empty() && !stopping_ && error_ == nullptr) {
        task = queue_.front();
        queue_.pop_front();
        task.rect = intersection(task.rect, windows_[static_cast<std::size_t>(task.level)]);
        if (!task.rect.empty()) {
            return true;
        }
    }
    return false;
}

// Runs tasks until the queue is empty: each job keeps going rather than launching another.
void HorizonNormalStream::runJob() {
    while (true) {
        Task task;
        {
            const std::lock_guard lock(mutex_);
            if (!takeTask(task)) {
                --inFlight_;
                done_.notify_all();
                return;
            }
        }
        Result result{task.level, task.rect, {}};
        std::exception_ptr error;
        try {
            result.texels = normalTexels(sampler_.sample(task.level, task.rect));
        } catch (...) {
            error = std::current_exception();
        }
        const std::lock_guard lock(mutex_);
        if (error != nullptr) {
            if (error_ == nullptr) {
                error_ = error;
            }
        } else {
            results_.push_back(std::move(result));
        }
        done_.notify_all();
    }
}

void HorizonNormalStream::collect(std::vector<NormalRegion>& out) {
    std::vector<Result> results;
    std::exception_ptr error;
    std::vector<HorizonRect> windows;
    {
        const std::lock_guard lock(mutex_);
        std::swap(results, results_);
        std::swap(error, error_);
        windows = windows_;
    }
    if (error != nullptr) {
        std::rethrow_exception(error);
    }
    for (const Result& result : results) {
        const HorizonRect inside =
            intersection(result.rect, windows[static_cast<std::size_t>(result.level)]);
        if (inside.empty()) {
            continue;
        }
        samplesComputed_ += result.rect.count();
        const std::vector<std::uint16_t> part =
            inside == result.rect ? std::vector<std::uint16_t>{}
                                  : partTexels(result.rect, ratio(), result.texels, inside);
        for (NormalRegion& region : normalRegions(result.level, side_, ratio(), inside,
                                                  part.empty() ? result.texels : part)) {
            out.push_back(std::move(region));
        }
    }
    launchJobs(); // queued while every job was busy
}

void HorizonNormalStream::waitIdle() {
    std::unique_lock lock(mutex_);
    done_.wait(lock, [this] { return error_ != nullptr || (inFlight_ == 0 && queue_.empty()); });
}

bool HorizonNormalStream::busy() {
    const std::lock_guard lock(mutex_);
    return inFlight_ > 0 || !queue_.empty() || !results_.empty();
}

std::vector<std::uint16_t> partTexels(const HorizonRect& rect, int ratio,
                                      std::span<const std::uint16_t> texels,
                                      const HorizonRect& part) {
    const std::int64_t width = rect.width() * ratio * kNormalHalves;
    const std::int64_t partWidth = part.width() * ratio * kNormalHalves;
    std::vector<std::uint16_t> out;
    out.reserve(static_cast<std::size_t>(partWidth * part.height() * ratio));
    for (std::int64_t row = (part.z0 - rect.z0) * ratio; row < (part.z1 - rect.z0) * ratio; ++row) {
        const auto begin = texels.begin() + static_cast<std::ptrdiff_t>(
                                                row * width + (part.x0 - rect.x0) * ratio * kNormalHalves);
        out.insert(out.end(), begin, begin + static_cast<std::ptrdiff_t>(partWidth));
    }
    return out;
}

} // namespace terrain::unreal

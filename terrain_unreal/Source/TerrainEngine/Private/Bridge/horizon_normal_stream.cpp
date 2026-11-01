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
        if (!sampler_.hasDetail(change.level)) {
            continue;
        }
        for (const HorizonRect& rect : change.written) {
            // The textures are created pending: nothing to mark before the first commit.
            for (NormalRegion& region : placed_ && ratio() > 0
                                            ? pendingRegions(change.level, side_, ratio(), rect)
                                            : std::vector<NormalRegion>{}) {
                out.push_back(std::move(region));
            }
            for (const HorizonRect& tile : detailTiles(rect, params_.jobSamples)) {
                tasks.push_back({change.level, tile});
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

void HorizonNormalStream::dropQueued() {
    const std::lock_guard lock(mutex_);
    queue_.clear();
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
        Result result;
        std::exception_ptr error;
        try {
            result = compute(task);
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

// The detail of a task: the texels of its normals, the point and raised heights of its samples.
HorizonNormalStream::Result HorizonNormalStream::compute(const Task& task) const {
    const HorizonDetail detail = sampler_.sample(task.level, task.rect);
    Result result{task.level, task.rect, {}, detail.points, detail.points};
    if (ratio() > 0) {
        result.texels = normalTexels(detail.slopes);
    }
    for (std::size_t n = 0; n < detail.lifts.size(); ++n) {
        if (detail.lifts[n] > 0.0f) {
            result.raised[n] = detail.points[n] + detail.lifts[n]; // as HorizonSampler raises
        }
    }
    return result;
}

void HorizonNormalStream::collect(std::vector<NormalRegion>& normals,
                                  std::vector<CrestBand>& crests) {
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
        collectResult(result, inside, normals, crests);
    }
    launchJobs(); // queued while every job was busy
}

// The normals of a result and its crests where some sample rises, for its samples `inside`.
void HorizonNormalStream::collectResult(const Result& result, const HorizonRect& inside,
                                        std::vector<NormalRegion>& normals,
                                        std::vector<CrestBand>& crests) const {
    const bool whole = inside == result.rect;
    if (!result.texels.empty()) {
        const std::vector<std::uint16_t> part =
            whole ? std::vector<std::uint16_t>{}
                  : partTexels(result.rect, ratio(), result.texels, inside);
        for (NormalRegion& region :
             normalRegions(result.level, side_, ratio(), inside, whole ? result.texels : part)) {
            normals.push_back(std::move(region));
        }
    }
    if (result.points == result.raised) {
        return; // nothing rises, or no crest at this level
    }
    crests.push_back({result.level, inside,
                      whole ? result.points : partValues(result.rect, result.points, inside),
                      whole ? result.raised : partValues(result.rect, result.raised, inside)});
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

std::vector<HorizonRect> detailTiles(const HorizonRect& rect, std::int64_t samples) {
    if (rect.empty()) {
        return {};
    }
    std::int64_t side = 1;
    while ((side + 1) * (side + 1) <= samples) {
        ++side;
    }
    // A band thinner than a tile keeps its whole thin side.
    std::int64_t tileWidth = side;
    if (rect.width() <= side) {
        tileWidth = rect.width();
    } else if (rect.height() < side) {
        tileWidth = std::min(rect.width(), std::max<std::int64_t>(1, samples / rect.height()));
    }
    const std::int64_t tileHeight =
        std::min(rect.height(), std::max<std::int64_t>(1, samples / tileWidth));
    const std::int64_t columns = (rect.width() + tileWidth - 1) / tileWidth;
    const std::int64_t rows = (rect.height() + tileHeight - 1) / tileHeight;
    std::vector<HorizonRect> tiles;
    for (std::int64_t r = 0; r < rows; ++r) {
        const std::int64_t z0 = rect.z0 + rect.height() * r / rows;
        const std::int64_t z1 = rect.z0 + rect.height() * (r + 1) / rows;
        for (std::int64_t c = 0; c < columns; ++c) {
            tiles.push_back({rect.x0 + rect.width() * c / columns, z0,
                             rect.x0 + rect.width() * (c + 1) / columns, z1});
        }
    }
    return tiles;
}

} // namespace terrain::unreal

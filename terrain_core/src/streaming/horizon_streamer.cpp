#include "terrain/streaming/horizon_streamer.hpp"

#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace terrain {

HorizonStreamer::HorizonStreamer(const HorizonSampler& sampler, const HorizonStreamParams& params,
                                 HorizonEditMap* edits)
    : sampler_(sampler), params_(params), edits_(edits), clipmap_(sampler.params()) {
    if (params_.workerThreads < 0 || params_.pieceSamples == 0) {
        throw std::invalid_argument("HorizonStreamParams: invalid thread count or piece size");
    }
    if (edits_ != nullptr) {
        edits_->validate(sampler.params());
    }
    for (int i = 0; i < params_.workerThreads; ++i) {
        workers_.emplace_back([this] { workerLoop(); });
    }
}

HorizonStreamer::~HorizonStreamer() {
    {
        const std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    workAvailable_.notify_all();
    workers_.clear();
}

HorizonChanges HorizonStreamer::update(double x, double z) {
    rethrowError();
    HorizonChanges changes;
    commitIfComplete(changes);
    if (epoch_ == nullptr) {
        // No worker reads the edits now: the staged ones may take effect.
        if (edits_ != nullptr && edits_->hasStaged()) {
            for (const HorizonArea& area : edits_->applyStaged()) {
                clipmap_.invalidate(area.minX, area.minZ, area.maxX, area.maxZ);
            }
        }
        if (std::optional<HorizonEpoch> next = clipmap_.plan(x, z, params_.pieceSamples)) {
            startEpoch(std::move(*next));
            commitIfComplete(changes); // an epoch with nothing to compute
        }
    }
    return changes;
}

std::size_t HorizonStreamer::generatePending(std::size_t maxPieces) {
    std::unique_lock lock(mutex_);
    std::size_t generated = 0;
    while (generated < maxPieces && !queue_.empty()) {
        runJob(lock);
        ++generated;
    }
    return generated;
}

void HorizonStreamer::waitIdle() {
    if (workers_.empty()) {
        generatePending(std::numeric_limits<std::size_t>::max());
    } else {
        std::unique_lock lock(mutex_);
        workDone_.wait(lock, [this] { return error_ != nullptr || remaining_ == 0; });
    }
    rethrowError();
}

void HorizonStreamer::invalidate(double minX, double minZ, double maxX, double maxZ) {
    clipmap_.invalidate(minX, minZ, maxX, maxZ);
}

void HorizonStreamer::startEpoch(HorizonEpoch epoch) {
    epoch_ = std::make_unique<HorizonEpoch>(std::move(epoch));
    {
        const std::lock_guard lock(mutex_);
        queue_.clear();
        for (auto piece = epoch_->pieces.rbegin(); piece != epoch_->pieces.rend(); ++piece) {
            queue_.push_back(&*piece);
        }
        remaining_ = queue_.size();
    }
    workAvailable_.notify_all();
}

void HorizonStreamer::commitIfComplete(HorizonChanges& changes) {
    if (epoch_ == nullptr) {
        return;
    }
    {
        const std::lock_guard lock(mutex_);
        if (remaining_ != 0 || error_ != nullptr) {
            return;
        }
    }
    HorizonChanges committed = clipmap_.commit(*epoch_);
    epoch_.reset();
    for (HorizonLevelChange& change : committed.levels) {
        changes.levels.push_back(std::move(change));
    }
}

// Forgets the epoch in flight once no worker computes a piece of it, and the errors of its
// other pieces.
void HorizonStreamer::dropEpoch() {
    {
        std::unique_lock lock(mutex_);
        queue_.clear();
        workDone_.wait(lock, [this] { return inFlight_ == 0; });
        remaining_ = 0;
        error_ = nullptr;
    }
    epoch_.reset();
}

// Computes the piece at the back of the queue with the lock released.
void HorizonStreamer::runJob(std::unique_lock<std::mutex>& lock) {
    HorizonPiece* piece = queue_.back();
    queue_.pop_back();
    ++inFlight_;
    lock.unlock();
    std::exception_ptr error;
    try {
        computePiece(sampler_, *piece);
    } catch (...) {
        error = std::current_exception();
    }
    lock.lock();
    --inFlight_;
    --remaining_;
    if (error != nullptr && error_ == nullptr) {
        error_ = error;
    }
    workDone_.notify_all();
}

void HorizonStreamer::workerLoop() {
    std::unique_lock lock(mutex_);
    while (true) {
        workAvailable_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
        if (stopping_) {
            return;
        }
        runJob(lock);
    }
}

void HorizonStreamer::rethrowError() {
    std::exception_ptr error;
    {
        const std::lock_guard lock(mutex_);
        std::swap(error, error_);
    }
    if (error != nullptr) {
        dropEpoch(); // planned again by the next update, with its invalidations
        std::rethrow_exception(error);
    }
}

} // namespace terrain

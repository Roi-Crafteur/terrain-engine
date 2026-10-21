#include "horizon_epochs.hpp"

#include <optional>
#include <stdexcept>
#include <utility>

namespace terrain::unreal {

HorizonEpochRunner::HorizonEpochRunner(const HorizonSampler& sampler, JobLauncher launch,
                                       PiecePreparer prepare, const HorizonEpochParams& params)
    : sampler_(sampler), launch_(std::move(launch)), prepare_(std::move(prepare)),
      params_(params), clipmap_(sampler.params()) {
    if (!launch_ || params_.maxPiecesInFlight < 1 || params_.pieceSamples == 0) {
        throw std::invalid_argument("HorizonEpochRunner: a launcher, at least one piece in flight "
                                    "and pieces of at least one sample expected");
    }
}

HorizonEpochRunner::~HorizonEpochRunner() {
    std::unique_lock lock(mutex_);
    stopping_ = true;
    done_.wait(lock, [this] { return inFlight_ == 0; });
}

EpochCommit HorizonEpochRunner::update(double x, double z) {
    rethrowError();
    EpochCommit result;
    commitIfComplete(result);
    if (epoch_ == nullptr) {
        if (std::optional<HorizonEpoch> next = clipmap_.plan(x, z, params_.pieceSamples)) {
            start(std::move(*next));
            commitIfComplete(result); // computed at once (synchronous jobs, nothing to compute)
        }
    }
    return result;
}

void HorizonEpochRunner::waitIdle() {
    if (epoch_ == nullptr) {
        return;
    }
    std::unique_lock lock(mutex_);
    done_.wait(lock, [this] { return error_ != nullptr || remaining_ == 0; });
}

void HorizonEpochRunner::start(HorizonEpoch epoch) {
    epoch_ = std::make_unique<HorizonEpoch>(std::move(epoch));
    prepared_.assign(epoch_->pieces.size(), {});
    std::vector<std::size_t> first;
    {
        const std::lock_guard lock(mutex_);
        next_ = 0;
        remaining_ = epoch_->pieces.size();
        while (static_cast<int>(inFlight_) < params_.maxPiecesInFlight) {
            const std::size_t index = takeNext();
            if (index == epoch_->pieces.size()) {
                break;
            }
            first.push_back(index);
        }
    }
    for (const std::size_t index : first) {
        launch_([this, index] { runPiece(index); });
    }
}

std::size_t HorizonEpochRunner::takeNext() {
    const std::size_t count = epoch_->pieces.size();
    if (stopping_ || error_ != nullptr || next_ >= count) {
        return count;
    }
    ++inFlight_;
    return next_++;
}

// Computes a piece (and prepares it), then launches the next one: at most maxPiecesInFlight
// jobs run at once. Nothing of the runner is touched after the lock is released, but the launch.
void HorizonEpochRunner::runPiece(std::size_t index) {
    std::exception_ptr error;
    std::vector<float> data;
    try {
        HorizonPiece& piece = epoch_->pieces[index];
        computePiece(sampler_, piece);
        if (prepare_) {
            data = prepare_(piece);
        }
    } catch (...) {
        error = std::current_exception();
    }
    prepared_[index] = std::move(data);
    std::size_t next = 0;
    std::size_t count = 0;
    {
        const std::lock_guard lock(mutex_);
        --inFlight_;
        --remaining_;
        if (error != nullptr && error_ == nullptr) {
            error_ = error;
        }
        count = epoch_->pieces.size();
        next = takeNext();
        done_.notify_all();
    }
    if (next < count) {
        launch_([this, next] { runPiece(next); });
    }
}

void HorizonEpochRunner::commitIfComplete(EpochCommit& result) {
    if (epoch_ == nullptr) {
        return;
    }
    {
        const std::lock_guard lock(mutex_);
        if (remaining_ != 0 || error_ != nullptr) {
            return;
        }
    }
    HorizonChanges changes = clipmap_.commit(*epoch_);
    for (HorizonLevelChange& change : changes.levels) {
        result.changes.levels.push_back(std::move(change));
    }
    for (std::size_t i = 0; i < epoch_->pieces.size(); ++i) {
        const HorizonPiece& piece = epoch_->pieces[i];
        result.pieces.push_back({piece.level, piece.rect, std::move(prepared_[i])});
    }
    epoch_.reset();
    prepared_.clear();
}

// Forgets the epoch in flight once no job computes a piece of it, and the errors of the others.
void HorizonEpochRunner::dropEpoch() {
    {
        std::unique_lock lock(mutex_);
        next_ = epoch_ != nullptr ? epoch_->pieces.size() : 0;
        done_.wait(lock, [this] { return inFlight_ == 0; });
        remaining_ = 0;
        error_ = nullptr;
    }
    epoch_.reset();
    prepared_.clear();
}

void HorizonEpochRunner::rethrowError() {
    std::exception_ptr error;
    {
        const std::lock_guard lock(mutex_);
        std::swap(error, error_);
        if (error != nullptr && epoch_ != nullptr) {
            next_ = epoch_->pieces.size(); // no more pieces of this epoch
        }
    }
    if (error != nullptr) {
        dropEpoch(); // planned again by the next update
        std::rethrow_exception(error);
    }
}

} // namespace terrain::unreal

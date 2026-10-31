#include "horizon_epochs.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>

namespace terrain::unreal {

HorizonEpochRunner::HorizonEpochRunner(const HorizonSampler& sampler, JobLauncher launch,
                                       PiecePreparer prepare, const HorizonEpochParams& params,
                                       HorizonEditMap* edits)
    : sampler_(sampler), launch_(std::move(launch)), prepare_(std::move(prepare)),
      params_(params), edits_(edits), clipmap_(sampler.params()) {
    if (!launch_ || params_.maxPiecesInFlight < 1 || params_.pieceSamples == 0) {
        throw std::invalid_argument("HorizonEpochRunner: a launcher, at least one piece in flight "
                                    "and pieces of at least one sample expected");
    }
    if (edits_ != nullptr) {
        edits_->validate(sampler.params());
    }
    maxPieces_ = params_.maxPiecesInFlight;
}

HorizonEpochRunner::~HorizonEpochRunner() {
    std::unique_lock lock(mutex_);
    stopping_ = true;
    done_.wait(lock, [this] { return inFlight_ == 0; });
}

EpochCommit HorizonEpochRunner::update(double x, double z) {
    noteNewPlace(x, z);
    EpochCommit result;
    if (!settleAbandoned()) {
        return result; // pieces of the old place still running
    }
    rethrowError();
    commitIfComplete(result);
    if (epoch_ == nullptr) {
        // No job reads the edits now: the staged ones may take effect.
        if (edits_ != nullptr && edits_->hasStaged()) {
            for (const HorizonArea& area : edits_->applyStaged()) {
                clipmap_.invalidate(area.minX, area.minZ, area.maxX, area.maxZ);
            }
        }
        planNext(x, z);
        commitIfComplete(result); // computed at once (synchronous jobs, nothing to compute)
    }
    return result;
}

// A new place: the player out of the window of the finest level, and no epoch on its way there.
// The levels of the old place are no longer shown; the epoch in flight is abandoned.
void HorizonEpochRunner::noteNewPlace(double x, double z) {
    const HorizonLevel& finest = clipmap_.level(0);
    if (!params_.progressive || (finest.placed() && finest.heightAt(x, z).has_value())) {
        return;
    }
    if (epoch_ != nullptr && !abandoned_ && near(epochX_, epochZ_, x, z)) {
        return; // planned around this place
    }
    levelsShown_ = 0;
    if (epoch_ != nullptr && !abandoned_) {
        const std::lock_guard lock(mutex_);
        next_ = epoch_->pieces.size(); // no more pieces of it
        abandoned_ = true;
    }
}

// Forgets an abandoned epoch once none of its pieces is computed any more; false until then.
bool HorizonEpochRunner::settleAbandoned() {
    if (!abandoned_) {
        return true;
    }
    {
        const std::lock_guard lock(mutex_);
        if (inFlight_ > 0) {
            return false;
        }
        remaining_ = 0;
        error_ = nullptr; // an error of the old place is no longer of interest
    }
    epoch_.reset();
    prepared_.clear();
    abandoned_ = false;
    return true;
}

// The next epoch: one more level at a new place, finest first (skipping the levels already
// around the player), then every level at once.
void HorizonEpochRunner::planNext(double x, double z) {
    const int levels = clipmap_.levelCount();
    while (params_.progressive && levelsShown_ < levels) {
        std::optional<HorizonEpoch> next =
            clipmap_.planFinest(x, z, levelsShown_ + 1, params_.pieceSamples);
        if (next) {
            epochX_ = x;
            epochZ_ = z;
            start(std::move(*next));
            return;
        }
        ++levelsShown_; // that level stands around the player already
    }
    if (std::optional<HorizonEpoch> next = clipmap_.plan(x, z, params_.pieceSamples)) {
        epochX_ = x;
        epochZ_ = z;
        start(std::move(*next));
    }
}

// Two places within half of the half window of the finest level of each other, any images.
bool HorizonEpochRunner::near(double ax, double az, double bx, double bz) const {
    const HorizonParams& params = clipmap_.params();
    const double reach = 0.25 * params.cellsPerSide * levelPitch(params, 0);
    const double dx = nearestImage(bx, ax, params.world.x) - ax;
    const double dz = nearestImage(bz, az, params.world.z) - az;
    return std::abs(dx) <= reach && std::abs(dz) <= reach;
}

void HorizonEpochRunner::waitIdle() {
    if (epoch_ == nullptr) {
        return;
    }
    const bool abandoned = abandoned_;
    std::unique_lock lock(mutex_);
    done_.wait(lock, [this, abandoned] {
        return error_ != nullptr || (abandoned ? inFlight_ == 0 : remaining_ == 0);
    });
}

void HorizonEpochRunner::setMaxPiecesInFlight(int pieces) {
    std::vector<std::size_t> launches;
    {
        const std::lock_guard lock(mutex_);
        maxPieces_ = std::max(1, pieces);
        if (epoch_ != nullptr) {
            const std::size_t count = epoch_->pieces.size();
            for (std::size_t index = takeNext(); index < count; index = takeNext()) {
                launches.push_back(index);
            }
        }
    }
    for (const std::size_t index : launches) {
        launch_([this, index] { runPiece(index); });
    }
}

int HorizonEpochRunner::maxPiecesInFlight() const {
    const std::lock_guard lock(mutex_);
    return maxPieces_;
}

void HorizonEpochRunner::start(HorizonEpoch epoch) {
    epoch_ = std::make_unique<HorizonEpoch>(std::move(epoch));
    prepared_.assign(epoch_->pieces.size(), {});
    std::vector<std::size_t> first;
    {
        const std::lock_guard lock(mutex_);
        next_ = 0;
        remaining_ = epoch_->pieces.size();
        while (static_cast<int>(inFlight_) < maxPieces_) {
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
    if (stopping_ || error_ != nullptr || next_ >= count ||
        static_cast<int>(inFlight_) >= maxPieces_) {
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
    if (epoch_ == nullptr || abandoned_) {
        return;
    }
    {
        const std::lock_guard lock(mutex_);
        if (remaining_ != 0 || error_ != nullptr) {
            return;
        }
    }
    HorizonChanges changes = clipmap_.commit(*epoch_);
    levelsShown_ = std::max(levelsShown_, static_cast<int>(epoch_->centers.size()));
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

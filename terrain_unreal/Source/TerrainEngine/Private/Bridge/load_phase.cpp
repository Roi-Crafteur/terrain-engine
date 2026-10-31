#include "load_phase.hpp"

#include "terrain/det_math.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace terrain::unreal {

namespace {

// The cheapest upload the budget assumes, milliseconds: a frame never takes more than its maximum.
constexpr double kCheapestUploadMilliseconds = 0.05;
constexpr double kMaxPitchDegrees = 89.9;

} // namespace

int defaultLoadingCap(unsigned hardwareThreads, const LoadBudget& normal) {
    const int threads = static_cast<int>(std::min(hardwareThreads, 1024U));
    return std::max(std::min(threads - 4, 12), normal.workers + normal.pieces);
}

LoadPhase::LoadPhase(const LoadPhaseParams& params) : params_(params) {
    const LoadBudget& normal = params_.normal;
    if (params_.cap < 1 || normal.workers < 1 || normal.pieces < 1 ||
        normal.uploadsPerFrame < 1 || params_.maxUploadsPerFrame < normal.uploadsPerFrame ||
        !(params_.uploadMillisecondsPerFrame >= 0.0) || !(params_.fadeMaxSeconds >= 0.0) ||
        !(params_.timeoutSeconds >= 0.0) || params_.nearHorizonLevels < 0) {
        throw std::invalid_argument("LoadPhase: invalid parameters");
    }
}

void LoadPhase::begin(double now) {
    started_ = true;
    start_ = now;
    loading_ = true;
    fading_ = params_.fade;
    holding_ = true;
    ready_ = false;
    readyTold_ = false;
    prepared_.reset();
    missing_.clear();
}

void LoadPhase::prepare(double x, double z, double now) {
    begin(now);
    fading_ = false;
    holding_ = false;
    prepared_ = std::pair(x, z);
}

void LoadPhase::release() {
    prepared_.reset();
}

bool LoadPhase::nearShown(const LoadStatus& status) const {
    const bool ground = prepared_ ? status.groundTiles : status.groundCollision;
    const int near = std::min(params_.nearHorizonLevels, status.horizonLevelCount);
    return ground && status.ring64 && status.allTiles && status.horizonLevels >= near;
}

void LoadPhase::update(const LoadStatus& status, double now) {
    if (!started_) {
        return;
    }
    const double seconds = now - start_;
    if (holding_ && status.groundCollision) {
        holding_ = false;
    }
    const bool near = nearShown(status);
    if (fading_ && near) {
        fading_ = false;
    } else if (fading_ && seconds >= params_.fadeMaxSeconds) {
        fading_ = false;
        const int levels = std::min(params_.nearHorizonLevels, status.horizonLevelCount);
        const std::pair<bool, std::string> parts[] = {
            {status.groundCollision, "ground"},
            {status.ring64, "64 m"},
            {status.allTiles, "tiles"},
            {status.horizonLevels >= levels, "horizon 0-" + std::to_string(levels - 1)}};
        for (const auto& [shown, name] : parts) {
            if (!shown) {
                missing_.push_back(name);
            }
        }
    }
    if (prepared_ && !readyTold_ && near) {
        ready_ = true;
        readyTold_ = true;
    }
    const bool horizonAll =
        status.horizonLevelCount == 0 || status.horizonLevels >= status.horizonLevelCount;
    if (loading_ && ((status.allTiles && horizonAll) || seconds >= params_.timeoutSeconds)) {
        loading_ = false;
    }
}

bool LoadPhase::takeReady() {
    return std::exchange(ready_, false);
}

std::vector<std::string> LoadPhase::takeMissingAtCap() {
    return std::exchange(missing_, {});
}

LoadBudget LoadPhase::budget(bool tilesPending, double meanUploadMilliseconds) const {
    const LoadBudget& normal = params_.normal;
    if (!loading_) {
        return normal;
    }
    const int cap = std::max(params_.cap, normal.workers + normal.pieces);
    LoadBudget result;
    if (tilesPending) {
        result.workers = std::max(normal.workers, (cap + 1) / 2);
        result.pieces = std::max(normal.pieces, cap - result.workers);
    } else {
        result.workers = normal.workers;
        result.pieces = std::max(normal.pieces, cap - normal.workers);
    }
    const double mean = std::max(meanUploadMilliseconds, kCheapestUploadMilliseconds);
    const auto byTime = static_cast<std::size_t>(params_.uploadMillisecondsPerFrame / mean);
    result.uploadsPerFrame = std::clamp(byTime, normal.uploadsPerFrame, params_.maxUploadsPerFrame);
    return result;
}

ViewAngles viewAngles(double headingDegrees, double pitchDegrees) {
    double yaw = headingDegrees - 90.0;
    yaw -= 360.0 * static_cast<double>(dmath::floorIndex((yaw + 180.0) / 360.0).index);
    if (yaw <= -180.0) {
        yaw += 360.0;
    }
    return {std::clamp(pitchDegrees, -kMaxPitchDegrees, kMaxPitchDegrees), yaw};
}

UnrealPoint lookDirection(const ViewAngles& angles) {
    const double pitch = angles.pitch / 360.0; // turns
    const double yaw = angles.yaw / 360.0;
    const double flat = dmath::cosTurns(pitch);
    return {flat * dmath::cosTurns(yaw), flat * dmath::sinTurns(yaw), dmath::sinTurns(pitch)};
}

} // namespace terrain::unreal

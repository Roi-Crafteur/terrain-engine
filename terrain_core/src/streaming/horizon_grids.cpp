#include "terrain/streaming/horizon_grids.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace terrain {

namespace {

// Position of v along an axis of the window, in samples from its low corner, or nullopt when
// outside [0, cells].
std::optional<double> windowCoordinate(double v, std::int64_t center, std::int64_t low, int pitch,
                                       double period, int cells) {
    const double image = nearestImage(v, samplePosition(center, pitch), period);
    const double u = image / pitch - static_cast<double>(low); // a power of two: exact
    if (!(u >= 0.0 && u <= cells)) {
        return std::nullopt;
    }
    return u;
}

} // namespace

HorizonLevel::HorizonLevel(const HorizonParams& params, int level)
    : level_(level), pitch_(0), side_(samplesPerSide(params)), counts_(), world_(params.world) {
    validate(params);
    if (level < 0 || level >= params.levels) {
        throw std::invalid_argument("HorizonLevel: no such level");
    }
    pitch_ = levelPitch(params, level);
    counts_ = levelCounts(params, level);
    slots_.resize(static_cast<std::size_t>(side_) * static_cast<std::size_t>(side_));
}

HorizonRect HorizonLevel::window() const { return windowAround(center_, side_ - 1); }

void HorizonLevel::moveTo(HorizonIndex center) {
    center_ = center;
    placed_ = true;
}

void HorizonLevel::write(const HorizonRect& rect, std::span<const HorizonCell> cells) {
    if (rect.empty() && cells.empty()) {
        return;
    }
    if (!placed_ || rect.empty() || !(intersection(rect, window()) == rect)) {
        throw std::invalid_argument("HorizonLevel: the rectangle is outside the window");
    }
    if (static_cast<std::int64_t>(cells.size()) != rect.count()) {
        throw std::invalid_argument("HorizonLevel: the cells do not fit the rectangle");
    }
    std::size_t n = 0;
    for (std::int64_t k = rect.z0; k < rect.z1; ++k) {
        for (std::int64_t i = rect.x0; i < rect.x1; ++i) {
            slots_[slotIndex({i, k})] = cells[n++];
        }
    }
}

const HorizonCell& HorizonLevel::at(HorizonIndex sample) const {
    if (!placed_ || !window().contains(sample)) {
        throw std::out_of_range("HorizonLevel: the sample is outside the window");
    }
    return slots_[slotIndex(sample)];
}

const HorizonCell& HorizonLevel::atWindow(int i, int k) const {
    const HorizonRect rect = window();
    return at({rect.x0 + i, rect.z0 + k});
}

const HorizonCell* HorizonLevel::find(HorizonIndex sample) const {
    if (!placed_) {
        return nullptr;
    }
    const HorizonIndex image{center_.x + wrapIndex(sample.x - center_.x, counts_.x),
                             center_.z + wrapIndex(sample.z - center_.z, counts_.z)};
    return window().contains(image) ? &slots_[slotIndex(image)] : nullptr;
}

HorizonIndex HorizonLevel::firstSlot() const {
    const HorizonRect rect = window();
    return slotOf({rect.x0, rect.z0}, side_);
}

double HorizonLevel::originX(double observerX) const {
    const double center = samplePosition(wrapIndex(center_.x, counts_.x), pitch_);
    return nearestImage(center, observerX, world_.x) - samplePosition((side_ - 1) / 2, pitch_);
}

double HorizonLevel::originZ(double observerZ) const {
    const double center = samplePosition(wrapIndex(center_.z, counts_.z), pitch_);
    return nearestImage(center, observerZ, world_.z) - samplePosition((side_ - 1) / 2, pitch_);
}

std::optional<float> HorizonLevel::heightAt(double x, double z) const {
    if (!placed_) {
        return std::nullopt;
    }
    const HorizonRect rect = window();
    const int cells = side_ - 1;
    const std::optional<double> u =
        windowCoordinate(x, center_.x, rect.x0, pitch_, world_.x, cells);
    const std::optional<double> v =
        windowCoordinate(z, center_.z, rect.z0, pitch_, world_.z, cells);
    if (!u || !v) {
        return std::nullopt;
    }
    const int i = std::min(static_cast<int>(std::floor(*u)), cells - 1);
    const int k = std::min(static_cast<int>(std::floor(*v)), cells - 1);
    const double s = *u - i;
    const double t = *v - k;
    const double top = atWindow(i, k).height * (1.0 - s) + atWindow(i + 1, k).height * s;
    const double bottom = atWindow(i, k + 1).height * (1.0 - s) + atWindow(i + 1, k + 1).height * s;
    return static_cast<float>(top * (1.0 - t) + bottom * t);
}

std::size_t HorizonLevel::slotIndex(HorizonIndex sample) const {
    const HorizonIndex slot = slotOf(sample, side_);
    return static_cast<std::size_t>(slot.z * side_ + slot.x);
}

} // namespace terrain

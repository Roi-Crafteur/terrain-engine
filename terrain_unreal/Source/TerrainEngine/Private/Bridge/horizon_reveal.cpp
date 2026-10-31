#include "horizon_reveal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace terrain::unreal {

namespace {

// Changes smaller than this are not sent again.
constexpr double kStep = 1e-3;

} // namespace

HorizonReveal::HorizonReveal(int levels, double seconds)
    : levels_(static_cast<std::size_t>(std::max(levels, 0))), seconds_(seconds) {}

const HorizonReveal::Level& HorizonReveal::at(int level) const {
    if (level < 0 || level >= static_cast<int>(levels_.size())) {
        throw std::out_of_range("HorizonReveal: no such level");
    }
    return levels_[static_cast<std::size_t>(level)];
}

void HorizonReveal::place(const LevelPlacement& placement, bool instant) {
    static_cast<void>(at(placement.level));
    Level& level = levels_[static_cast<std::size_t>(placement.level)];
    if (placement.visible && !level.visible) {
        level.reveal = instant ? 1.0 : 0.0;
    } else if (!placement.visible) {
        level.reveal = 0.0;
    }
    level.visible = placement.visible;
}

std::vector<int> HorizonReveal::advance(double seconds, bool instant) {
    const double step = seconds_ > 0.0 ? std::max(seconds, 0.0) / seconds_ : 1.0;
    for (Level& level : levels_) {
        if (level.visible) {
            level.reveal = instant ? 1.0 : std::min(1.0, level.reveal + step);
        }
    }
    std::vector<int> changed;
    for (int n = 0; n < static_cast<int>(levels_.size()); ++n) {
        Level& level = levels_[static_cast<std::size_t>(n)];
        const double edge = edgeFade(n);
        if (std::abs(level.reveal - level.sentReveal) >= kStep ||
            std::abs(edge - level.sentEdge) >= kStep ||
            (level.reveal == 1.0 && level.sentReveal != 1.0) ||
            (edge == 0.0 && level.sentEdge != 0.0)) {
            level.sentReveal = level.reveal;
            level.sentEdge = edge;
            changed.push_back(n);
        }
    }
    return changed;
}

double HorizonReveal::reveal(int level) const {
    return at(level).reveal;
}

double HorizonReveal::edgeFade(int level) const {
    static_cast<void>(at(level));
    if (level + 1 >= static_cast<int>(levels_.size())) {
        return 0.0;
    }
    const Level& coarser = levels_[static_cast<std::size_t>(level + 1)];
    return coarser.visible ? 1.0 - coarser.reveal : 1.0;
}

bool HorizonReveal::visible(int level) const {
    return at(level).visible;
}

} // namespace terrain::unreal

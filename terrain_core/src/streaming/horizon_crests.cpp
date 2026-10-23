#include "terrain/streaming/horizon_crests.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace terrain {

namespace {

// The point heights of a rectangle grown by one sample on each side, and its cells: cell
// (cu, cv) lies between the samples (x0 - 1 + cu, z0 - 1 + cv) and (x0 + cu, z0 + cv), for
// cu <= width and cv <= height. A corner may rise when it lies in the rectangle, on land.
struct Corners {
    HorizonRect rect;
    std::span<const float> heights;

    [[nodiscard]] std::int64_t side() const { return rect.width() + 2; }
    [[nodiscard]] std::int64_t cellsX() const { return rect.width() + 1; }
    [[nodiscard]] std::int64_t cellsZ() const { return rect.height() + 1; }
    // Point height of the corner (a, b) of cell (cu, cv), a and b in {0, 1}.
    [[nodiscard]] float at(std::int64_t cu, std::int64_t cv, int a, int b) const {
        return heights[static_cast<std::size_t>((cv + b) * side() + cu + a)];
    }
    [[nodiscard]] bool raisable(std::int64_t cu, std::int64_t cv, int a, int b) const {
        const std::int64_t i = cu + a - 1;
        const std::int64_t k = cv + b - 1;
        return i >= 0 && k >= 0 && i < rect.width() && k < rect.height() &&
               at(cu, cv, a, b) >= kSeaLevel;
    }
};

constexpr std::array<std::array<int, 2>, 4> kCorners{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}};

// The cells with a corner that may rise: the only ones whose top is needed.
std::vector<char> examinedCells(const Corners& corners) {
    std::vector<char> examined(static_cast<std::size_t>(corners.cellsX() * corners.cellsZ()), 0);
    for (std::int64_t cv = 0; cv < corners.cellsZ(); ++cv) {
        for (std::int64_t cu = 0; cu < corners.cellsX(); ++cu) {
            for (const auto& [a, b] : kCorners) {
                if (corners.raisable(cu, cv, a, b)) {
                    examined[static_cast<std::size_t>(cv * corners.cellsX() + cu)] = 1;
                }
            }
        }
    }
    return examined;
}

// Heights of the fine lattice of the cells, (cellsX * R + 1) x (cellsZ * R + 1) nodes row by
// row from the low corner of cell (0, 0): only the nodes on the sides and at the center of the
// examined cells are computed, the others stay 0. The nodes on samples take their point
// heights.
class FineLattice {
public:
    FineLattice(const Corners& corners, int ratio)
        : ratio_(ratio), width_(corners.cellsX() * ratio + 1),
          heights_(static_cast<std::size_t>(width_ * (corners.cellsZ() * ratio + 1)), 0.0f) {}

    [[nodiscard]] std::int64_t width() const { return width_; }
    [[nodiscard]] std::int64_t height() const {
        return static_cast<std::int64_t>(heights_.size()) / width_;
    }
    [[nodiscard]] float at(std::int64_t u, std::int64_t v) const {
        return heights_[static_cast<std::size_t>(v * width_ + u)];
    }
    float& at(std::int64_t u, std::int64_t v) {
        return heights_[static_cast<std::size_t>(v * width_ + u)];
    }
    // The cells along one axis whose closed span holds the fine index t: one, or two on a
    // border between cells; [first, last] clamped to [0, cells).
    [[nodiscard]] std::array<std::int64_t, 2> cellsOf(std::int64_t t, std::int64_t cells) const {
        const std::int64_t last = std::min(t / ratio_, cells - 1);
        const std::int64_t first = t % ratio_ == 0 && t > 0 ? t / ratio_ - 1 : last;
        return {first, last};
    }

private:
    int ratio_;
    std::int64_t width_;
    std::vector<float> heights_;
};

// Whether the node (u, v) lies on a side or at the center of an examined cell.
bool needed(const FineLattice& lattice, const Corners& corners,
            const std::vector<char>& examined, int ratio, std::int64_t u, std::int64_t v) {
    const std::int64_t a = u % ratio;
    const std::int64_t b = v % ratio;
    if (a != 0 && b != 0 && (2 * a != ratio || 2 * b != ratio)) {
        return false;
    }
    const auto [u0, u1] = lattice.cellsOf(u, corners.cellsX());
    const auto [v0, v1] = lattice.cellsOf(v, corners.cellsZ());
    for (std::int64_t cv = v0; cv <= v1; ++cv) {
        for (std::int64_t cu = u0; cu <= u1; ++cu) {
            if (examined[static_cast<std::size_t>(cv * corners.cellsX() + cu)] != 0) {
                return true;
            }
        }
    }
    return false;
}

// The canonical positions of a level's fine lattice, from the fine index of node (0, 0).
struct FineFrame {
    HorizonIndex first;
    HorizonIndex counts;
    int spacing = 1;

    [[nodiscard]] double x(std::int64_t u) const {
        return samplePosition(wrapIndex(first.x + u, counts.x), spacing);
    }
    [[nodiscard]] double z(std::int64_t v) const {
        return samplePosition(wrapIndex(first.z + v, counts.z), spacing);
    }
};

// Fills the nodes of the examined cells, one batch of heightsAt per fine row.
void fillLattice(const Heightfield& field, const FineFrame& frame, const Corners& corners,
                 const std::vector<char>& examined, int ratio, FineLattice& lattice) {
    std::vector<double> xs;
    std::vector<double> zs;
    std::vector<std::int64_t> columns;
    std::vector<float> out;
    for (std::int64_t v = 0; v < lattice.height(); ++v) {
        xs.clear();
        columns.clear();
        for (std::int64_t u = 0; u < lattice.width(); ++u) {
            if (!needed(lattice, corners, examined, ratio, u, v)) {
                continue;
            }
            if (u % ratio == 0 && v % ratio == 0) {
                lattice.at(u, v) = corners.at(u / ratio, v / ratio, 0, 0);
                continue;
            }
            xs.push_back(frame.x(u));
            columns.push_back(u);
        }
        zs.assign(xs.size(), frame.z(v));
        out.resize(xs.size());
        field.heightsAt(xs, zs, out);
        for (std::size_t n = 0; n < columns.size(); ++n) {
            lattice.at(columns[n], v) = out[n];
        }
    }
}

// Raises the highest corners of one examined cell to its top: best holds the largest lift of
// each sample of the rectangle so far.
void raiseCorners(const Corners& corners, const FineLattice& lattice, int ratio, std::int64_t cu,
                  std::int64_t cv, std::vector<double>& best) {
    const std::int64_t u0 = cu * ratio;
    const std::int64_t v0 = cv * ratio;
    float top = lattice.at(u0 + ratio / 2, v0 + ratio / 2);
    for (std::int64_t t = 0; t <= ratio; ++t) {
        top = std::max(top, std::max(lattice.at(u0 + t, v0), lattice.at(u0 + t, v0 + ratio)));
        top = std::max(top, std::max(lattice.at(u0, v0 + t), lattice.at(u0 + ratio, v0 + t)));
    }
    float high = corners.at(cu, cv, 0, 0);
    for (const auto& [a, b] : kCorners) {
        high = std::max(high, corners.at(cu, cv, a, b));
    }
    const double missed = static_cast<double>(top) - high;
    if (!(missed > 0.0)) {
        return;
    }
    for (const auto& [a, b] : kCorners) {
        if (corners.at(cu, cv, a, b) != high || !corners.raisable(cu, cv, a, b)) {
            continue;
        }
        double& largest =
            best[static_cast<std::size_t>((cv + b - 1) * corners.rect.width() + cu + a - 1)];
        largest = std::max(largest, missed);
    }
}

} // namespace

HorizonCrestSampler::HorizonCrestSampler(const Heightfield& field, const HorizonParams& params)
    : field_(&field), params_(params) {
    validate(params_);
    if (!(field.world() == params_.world)) {
        throw std::invalid_argument("HorizonCrestSampler: the relief and the horizon must share "
                                    "one world");
    }
}

bool HorizonCrestSampler::raises(int level) const {
    return params_.crestRatio > 0 && level >= 1;
}

void HorizonCrestSampler::fill(int level, const HorizonRect& rect,
                               std::span<const float> margined, std::span<float> lifts) const {
    if (level < 0 || level >= params_.levels) {
        throw std::invalid_argument("HorizonCrestSampler: no such level");
    }
    const std::int64_t grown = rect.empty() ? 0 : (rect.width() + 2) * (rect.height() + 2);
    if (static_cast<std::int64_t>(lifts.size()) != rect.count() ||
        static_cast<std::int64_t>(margined.size()) != grown) {
        throw std::invalid_argument("HorizonCrestSampler: the heights do not fit the rectangle");
    }
    std::fill(lifts.begin(), lifts.end(), 0.0f);
    if (rect.empty() || !raises(level)) {
        return;
    }
    const int ratio = params_.crestRatio;
    const Corners corners{rect, margined};
    const std::vector<char> examined = examinedCells(corners);
    const HorizonIndex counts = levelCounts(params_, level);
    const FineFrame frame{{(rect.x0 - 1) * ratio, (rect.z0 - 1) * ratio},
                          {counts.x * ratio, counts.z * ratio},
                          levelPitch(params_, level) / ratio};
    FineLattice lattice(corners, ratio);
    fillLattice(*field_, frame, corners, examined, ratio, lattice);
    std::vector<double> best(lifts.size(), 0.0);
    for (std::int64_t cv = 0; cv < corners.cellsZ(); ++cv) {
        for (std::int64_t cu = 0; cu < corners.cellsX(); ++cu) {
            if (examined[static_cast<std::size_t>(cv * corners.cellsX() + cu)] != 0) {
                raiseCorners(corners, lattice, ratio, cu, cv, best);
            }
        }
    }
    for (std::size_t n = 0; n < best.size(); ++n) {
        lifts[n] = static_cast<float>(best[n]);
    }
}

std::vector<float> HorizonCrestSampler::sample(int level, const HorizonRect& rect) const {
    std::vector<float> lifts(static_cast<std::size_t>(rect.count()), 0.0f);
    if (rect.empty() || level < 0 || level >= params_.levels) {
        fill(level, rect, {}, lifts);
        return lifts;
    }
    const int pitch = levelPitch(params_, level);
    const HorizonIndex counts = levelCounts(params_, level);
    const std::int64_t side = rect.width() + 2;
    std::vector<float> margined(static_cast<std::size_t>(side * (rect.height() + 2)));
    std::vector<double> xs(static_cast<std::size_t>(side));
    std::vector<double> zs(static_cast<std::size_t>(side));
    for (std::int64_t i = 0; i < side; ++i) {
        xs[static_cast<std::size_t>(i)] =
            samplePosition(wrapIndex(rect.x0 - 1 + i, counts.x), pitch);
    }
    for (std::int64_t k = 0; k < rect.height() + 2; ++k) {
        zs.assign(xs.size(), samplePosition(wrapIndex(rect.z0 - 1 + k, counts.z), pitch));
        field_->heightsAt(
            xs, zs,
            std::span<float>(margined).subspan(static_cast<std::size_t>(k * side),
                                               static_cast<std::size_t>(side)));
    }
    fill(level, rect, margined, lifts);
    return lifts;
}

} // namespace terrain

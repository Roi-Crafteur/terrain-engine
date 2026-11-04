// A lattice of the world telling which sites may reach a point (low_reach.hpp).
#include "low_reach.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terrain::lows {

namespace {

// The cell of the coordinate v of a side of period period cut in count cells of size, wrapped.
int cellOf(double v, double period, double size, int count) {
    const int c = static_cast<int>(std::floor((v + 0.5 * period) / size));
    return (c % count + count) % count;
}

} // namespace

ReachLattice::ReachLattice(const WorldSize& world, double cell) : world_(world) {
    if (!(cell > 0.0)) {
        throw std::invalid_argument("ReachLattice: the cells must have a size");
    }
    countX_ = std::max(1, static_cast<int>(std::lround(world.x / cell)));
    countZ_ = std::max(1, static_cast<int>(std::lround(world.z / cell)));
    sizeX_ = world.x / countX_;
    sizeZ_ = world.z / countZ_;
    offsets_.assign(static_cast<std::size_t>(countX_) * static_cast<std::size_t>(countZ_) + 1U, 0);
}

int ReachLattice::cellX(double x) const {
    return cellOf(x, world_.x, sizeX_, countX_);
}

int ReachLattice::cellZ(double z) const {
    return cellOf(z, world_.z, sizeZ_, countZ_);
}

void ReachLattice::add(std::uint32_t index, double x, double z, double reach) {
    const int i0 = cellX(x);
    const int j0 = cellZ(z);
    // The center within its cell, meters from the corner of the cell.
    const double ox = x + 0.5 * world_.x - i0 * sizeX_;
    const double oz = z + 0.5 * world_.z - j0 * sizeZ_;
    const int spanX = std::min(static_cast<int>(std::ceil(reach / sizeX_)) + 1, countX_ / 2);
    const int spanZ = std::min(static_cast<int>(std::ceil(reach / sizeZ_)) + 1, countZ_ / 2);
    for (int dj = -spanZ; dj <= spanZ; ++dj) {
        for (int di = -spanX; di <= spanX; ++di) {
            const double nx = std::clamp(ox, di * sizeX_, (di + 1) * sizeX_) - ox;
            const double nz = std::clamp(oz, dj * sizeZ_, (dj + 1) * sizeZ_) - oz;
            if (!(nx * nx + nz * nz < reach * reach)) {
                continue;
            }
            const int i = ((i0 + di) % countX_ + countX_) % countX_;
            const int j = ((j0 + dj) % countZ_ + countZ_) % countZ_;
            const auto cell = static_cast<std::uint64_t>(j) * static_cast<std::uint64_t>(countX_) +
                              static_cast<std::uint64_t>(i);
            marks_.push_back(cell << 32U | index);
        }
    }
}

void ReachLattice::finish() {
    // Sorted by cell, then index; a cell met twice by a site (a small world) counts it once.
    std::sort(marks_.begin(), marks_.end());
    marks_.erase(std::unique(marks_.begin(), marks_.end()), marks_.end());
    std::fill(offsets_.begin(), offsets_.end(), 0U);
    indices_.clear();
    indices_.reserve(marks_.size());
    for (const std::uint64_t mark : marks_) {
        ++offsets_[static_cast<std::size_t>(mark >> 32U) + 1U];
        indices_.push_back(static_cast<std::uint32_t>(mark & 0xFFFF'FFFFU));
    }
    for (std::size_t k = 1; k < offsets_.size(); ++k) {
        offsets_[k] += offsets_[k - 1];
    }
    marks_.clear();
    marks_.shrink_to_fit();
}

std::span<const std::uint32_t> ReachLattice::at(double x, double z) const {
    const auto cell = static_cast<std::size_t>(cellZ(z)) * static_cast<std::size_t>(countX_) +
                      static_cast<std::size_t>(cellX(x));
    if (cell + 1U >= offsets_.size() || indices_.empty()) {
        return {};
    }
    return std::span<const std::uint32_t>(indices_).subspan(offsets_[cell],
                                                            offsets_[cell + 1] - offsets_[cell]);
}

} // namespace terrain::lows

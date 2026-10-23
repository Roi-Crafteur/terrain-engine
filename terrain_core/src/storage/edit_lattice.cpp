#include "terrain/storage/edit_window.hpp"

#include "edit_cubes.hpp"

#include <algorithm>
#include <cassert>

namespace terrain {

namespace {

using edit_cubes::kCubeDistances;
using edit_cubes::nearestCube;
using edit_cubes::offsetIndex;

// The kinds of the samples of a lattice (EditLattice::classify).
constexpr std::uint8_t kAllRemoved = 1;
constexpr std::uint8_t kAllAdded = 2;
constexpr std::uint8_t kMixed = 3;

} // namespace

int EditLattice::Axis::coordinate(int index) const {
    return spaced() ? origin + (index / 3) * spacing + index % 3 - 1 : origin - 1 + index;
}

int EditLattice::Axis::indexOf(int c) const {
    const int t = c - origin + 1;
    if (t < 0) {
        return -1;
    }
    if (!spaced()) {
        return t < size() ? t : -1;
    }
    const int i = t / spacing;
    const int r = t % spacing;
    return r <= 2 && i < count ? 3 * i + r : -1;
}

std::pair<int, int> EditLattice::Axis::range(int low, int high) const {
    // t: a coordinate relative to the first one; sample i reaches t in [i s, i s + 2].
    const int tLow = low - origin + 1;
    const int tHigh = high - origin + 1;
    if (!spaced()) {
        return {std::max(tLow, 0), std::min(tHigh, size() - 1)};
    }
    const auto floorDiv = [this](int t) {
        return t >= 0 ? t / spacing : -((-t - 1) / spacing) - 1;
    };
    const int iLow = floorDiv(tLow);
    const int rLow = tLow - iLow * spacing;
    const int first = rLow <= 2 ? 3 * iLow + rLow : 3 * (iLow + 1);
    const int iHigh = floorDiv(tHigh);
    const int rHigh = tHigh - iHigh * spacing;
    const int last = 3 * iHigh + std::min(rHigh, 2);
    return {std::max(first, 0), std::min(last, size() - 1)};
}

EditLattice::EditLattice(const EditWindow& window, int x0, int y0, int z0, int spacing,
                         int count)
    : x_{x0, spacing, count}, y_{y0, spacing, count}, z_{z0, spacing, count} {
    assert(x_.size() <= 64); // a row of blocks along x in a word
    const BlockCoord min{x_.coordinate(0), y_.coordinate(0), z_.coordinate(0)};
    const BlockCoord max{x_.coordinate(x_.size() - 1), y_.coordinate(y_.size() - 1),
                         z_.coordinate(z_.size() - 1)};
    if (window.empty() || !window.meets(min, max)) {
        return;
    }
    const auto rows = static_cast<std::size_t>(y_.size()) * static_cast<std::size_t>(z_.size());
    removed_.assign(rows, 0);
    added_.assign(rows, 0);
    for (std::int32_t cz = min.z >> kSectionShift; cz <= max.z >> kSectionShift; ++cz) {
        for (std::int32_t cx = min.x >> kSectionShift; cx <= max.x >> kSectionShift; ++cx) {
            for (std::int32_t sy = sectionOf(min.y); sy <= sectionOf(max.y); ++sy) {
                fill(window, cx, sy, cz);
            }
        }
    }
    const auto edited = [](std::uint64_t bits) { return bits != 0; };
    if (std::none_of(removed_.begin(), removed_.end(), edited) &&
        std::none_of(added_.begin(), added_.end(), edited)) {
        std::vector<std::uint64_t>().swap(removed_);
        std::vector<std::uint64_t>().swap(added_);
        return;
    }
    classify();
}

void EditLattice::fill(const EditWindow& window, std::int32_t cx, std::int32_t sy,
                       std::int32_t cz) {
    const BlockSection* section = window.section(cx, sy, cz);
    if (section == nullptr) {
        return;
    }
    const BlockCoord base{cx * kSectionSide, sy * kSectionSide, cz * kSectionSide};
    const std::pair<int, int> xs = x_.range(std::max(base.x, window.minX_),
                                            std::min(base.x + kSectionSide - 1, window.maxX_));
    const std::pair<int, int> ys = y_.range(base.y, base.y + kSectionSide - 1);
    const std::pair<int, int> zs = z_.range(std::max(base.z, window.minZ_),
                                            std::min(base.z + kSectionSide - 1, window.maxZ_));
    if (xs.first > xs.second || ys.first > ys.second || zs.first > zs.second) {
        return;
    }
    if (const std::optional<BlockEdit> uniform = section->uniform()) {
        const std::uint64_t bits = ((~std::uint64_t{0}) >> (63 - (xs.second - xs.first)))
                                   << xs.first;
        std::vector<std::uint64_t>& rows = *uniform == BlockEdit::Removed ? removed_ : added_;
        for (int iz = zs.first; iz <= zs.second; ++iz) {
            for (int iy = ys.first; iy <= ys.second; ++iy) {
                rows[row(iy, iz)] |= bits;
            }
        }
        return;
    }
    if (section->count() <= BlockSection::kSparseLimit) {
        section->forEach([&](int index, BlockEdit edit) {
            const int x = base.x + index % kSectionSide;
            const int ix = x_.indexOf(x);
            const int iy = y_.indexOf(base.y + index / (kSectionSide * kSectionSide));
            const int iz = z_.indexOf(base.z + (index / kSectionSide) % kSectionSide);
            if (ix >= xs.first && ix <= xs.second && iy >= 0 && iz >= zs.first &&
                iz <= zs.second) {
                (edit == BlockEdit::Removed ? removed_ : added_)[row(iy, iz)] |=
                    std::uint64_t{1} << ix;
            }
        });
        return;
    }
    SectionMask removed{};
    SectionMask added{};
    section->toMasks(removed, added);
    fillRows(removed, added, base, xs, ys, zs);
}

void EditLattice::fillRows(const SectionMask& removed, const SectionMask& added, BlockCoord base,
                           std::pair<int, int> xs, std::pair<int, int> ys,
                           std::pair<int, int> zs) {
    const std::uint64_t keep = ((~std::uint64_t{0}) >> (63 - (xs.second - xs.first))) << xs.first;
    // A contiguous axis: index = local x + shift.
    const int shift = base.x - x_.coordinate(0);
    const auto rowBits = [&](const SectionMask& mask, int ly, int lz) -> std::uint64_t {
        const int first = sectionIndex(0, ly, lz);
        const std::uint64_t bits =
            (mask[static_cast<std::size_t>(first / 64)] >> (first % 64)) & 0xFFFFU;
        if (!x_.spaced()) {
            return (shift >= 0 ? bits << shift : bits >> -shift) & keep;
        }
        if (bits == 0 || bits == 0xFFFFU) { // a row all generated or all edited: no gathering
            return bits == 0 ? 0 : keep;
        }
        std::uint64_t gathered = 0;
        for (int ix = xs.first; ix <= xs.second; ++ix) {
            gathered |= ((bits >> (x_.coordinate(ix) - base.x)) & 1U) << ix;
        }
        return gathered;
    };
    for (int iz = zs.first; iz <= zs.second; ++iz) {
        const int lz = z_.coordinate(iz) - base.z;
        for (int iy = ys.first; iy <= ys.second; ++iy) {
            const int ly = y_.coordinate(iy) - base.y;
            removed_[row(iy, iz)] |= rowBits(removed, ly, lz);
            added_[row(iy, iz)] |= rowBits(added, ly, lz);
        }
    }
}

void EditLattice::classify() {
    const int n = x_.count;
    kinds_.assign(static_cast<std::size_t>(n) * static_cast<std::size_t>(n) *
                      static_cast<std::size_t>(n),
                  0);
    for (int k = 0; k < n; ++k) {
        for (int j = 0; j < n; ++j) {
            std::uint64_t anyEdit = 0;
            std::uint64_t allRemoved = ~std::uint64_t{0};
            std::uint64_t allAdded = ~std::uint64_t{0};
            for (int dz = 0; dz < 3; ++dz) {
                for (int dy = 0; dy < 3; ++dy) {
                    const std::size_t r = row(y_.first(j) + dy, z_.first(k) + dz);
                    anyEdit |= removed_[r] | added_[r];
                    allRemoved &= removed_[r];
                    allAdded &= added_[r];
                }
            }
            for (int i = 0; i < n; ++i) {
                const int shift = x_.first(i);
                std::uint8_t kind = 0;
                if (((allRemoved >> shift) & 7U) == 7U) {
                    kind = kAllRemoved;
                } else if (((allAdded >> shift) & 7U) == 7U) {
                    kind = kAllAdded;
                } else if (((anyEdit >> shift) & 7U) != 0) {
                    kind = kMixed;
                }
                kinds_[static_cast<std::size_t>((k * n + j) * n + i)] = kind;
            }
        }
    }
}

float EditLattice::density(float generated, int i, int j, int k) const {
    if (kinds_.empty()) {
        return generated;
    }
    const int n = x_.count;
    switch (kinds_[static_cast<std::size_t>((k * n + j) * n + i)]) {
    case kAllRemoved: // the cube of the block at the sample is the nearest: -(-1/2)
        return std::max(generated, -kCubeDistances[offsetIndex(0, 0, 0)]);
    case kAllAdded:
        return std::min(generated, kCubeDistances[offsetIndex(0, 0, 0)]);
    case kMixed:
        break;
    default:
        return generated;
    }
    // The 27 blocks around the sample as bits, by offsetIndex.
    const int x = x_.first(i);
    std::uint32_t removed = 0;
    std::uint32_t added = 0;
    for (int dz = 0; dz < 3; ++dz) {
        for (int dy = 0; dy < 3; ++dy) {
            const std::size_t r = row(y_.first(j) + dy, z_.first(k) + dz);
            const auto shift = static_cast<unsigned>(3 * (dz * 3 + dy));
            removed |= static_cast<std::uint32_t>((removed_[r] >> x) & 7U) << shift;
            added |= static_cast<std::uint32_t>((added_[r] >> x) & 7U) << shift;
        }
    }
    float density = generated;
    if (added != 0) {
        density = std::min(density, nearestCube(added));
    }
    if (removed != 0) {
        density = std::max(density, -nearestCube(removed));
    }
    return density;
}

} // namespace terrain

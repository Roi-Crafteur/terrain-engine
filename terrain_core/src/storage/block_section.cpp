#include "terrain/storage/block_section.hpp"

#include <algorithm>
#include <cassert>

namespace terrain {

namespace {

constexpr std::uint16_t kRowBits = 0xFFFFU;

// Bits [x0, x1] of a row of 16 blocks.
std::uint16_t rowMask(int x0, int x1) {
    const unsigned width = static_cast<unsigned>(x1 - x0 + 1);
    return static_cast<std::uint16_t>(((1U << width) - 1U) << x0);
}

// The 16 bits of the row (y, z) of a mask held as words.
std::uint16_t rowOf(const std::uint64_t* words, int y, int z) {
    const int first = sectionIndex(0, y, z);
    return static_cast<std::uint16_t>((words[first / 64] >> (first % 64)) & kRowBits);
}

// Empties a vector and gives its memory back.
template <class T>
void release(std::vector<T>& values) {
    std::vector<T>().swap(values);
}

} // namespace

BlockSection BlockSection::filled(BlockEdit edit) {
    BlockSection section;
    (edit == BlockEdit::Removed ? section.removed_ : section.added_) = kSectionBlocks;
    return section;
}

BlockSection BlockSection::fromMasks(const SectionMask& removed, const SectionMask& added) {
    BlockSection section;
    section.masks_.assign(removed.begin(), removed.end());
    section.masks_.insert(section.masks_.end(), added.begin(), added.end());
    int removedCount = 0;
    int addedCount = 0;
    for (int w = 0; w < kSectionWords; ++w) {
        const auto word = static_cast<std::size_t>(w);
        assert((removed[word] & added[word]) == 0);
        removedCount += std::popcount(removed[word]);
        addedCount += std::popcount(added[word]);
    }
    section.removed_ = static_cast<std::uint16_t>(removedCount);
    section.added_ = static_cast<std::uint16_t>(addedCount);
    section.normalize();
    return section;
}

void BlockSection::toMasks(SectionMask& removed, SectionMask& added) const {
    removed.fill(0);
    added.fill(0);
    if (!masks_.empty()) {
        std::copy(masks_.begin(), masks_.begin() + kSectionWords, removed.begin());
        std::copy(masks_.begin() + kSectionWords, masks_.end(), added.begin());
        return;
    }
    forEach([&removed, &added](int index, BlockEdit edit) {
        SectionMask& mask = edit == BlockEdit::Removed ? removed : added;
        mask[static_cast<std::size_t>(index / 64)] |= std::uint64_t{1} << (index % 64);
    });
}

std::optional<BlockEdit> BlockSection::at(int index) const {
    if (!masks_.empty()) {
        const auto word = static_cast<std::size_t>(index / 64);
        const int bit = index % 64;
        if (((masks_[word] >> bit) & 1U) != 0) {
            return BlockEdit::Removed;
        }
        if (((masks_[word + kSectionWords] >> bit) & 1U) != 0) {
            return BlockEdit::Added;
        }
        return std::nullopt;
    }
    if (!cells_.empty()) {
        const auto it = std::lower_bound(
            cells_.begin(), cells_.end(), index,
            [](std::uint16_t cell, int value) { return (cell & kIndexMask) < value; });
        if (it == cells_.end() || (*it & kIndexMask) != index) {
            return std::nullopt;
        }
        return (*it & kRemovedFlag) != 0 ? BlockEdit::Removed : BlockEdit::Added;
    }
    return uniform();
}

void BlockSection::set(int index, std::optional<BlockEdit> edit) {
    assert(index >= 0 && index < kSectionBlocks);
    const std::optional<BlockEdit> old = at(index);
    if (old == edit) {
        return;
    }
    if (old) {
        --(*old == BlockEdit::Removed ? removed_ : added_);
    }
    if (edit) {
        ++(*edit == BlockEdit::Removed ? removed_ : added_);
    }
    if (masks_.empty() && cells_.empty() && old) {
        toDense(); // a filled section, now one block short
    }
    if (!masks_.empty()) {
        const auto word = static_cast<std::size_t>(index / 64);
        const std::uint64_t bit = std::uint64_t{1} << (index % 64);
        masks_[word] &= ~bit;
        masks_[word + kSectionWords] &= ~bit;
        if (edit) {
            masks_[word + (*edit == BlockEdit::Removed ? 0 : kSectionWords)] |= bit;
        }
    } else {
        const auto it = std::lower_bound(
            cells_.begin(), cells_.end(), index,
            [](std::uint16_t cell, int value) { return (cell & kIndexMask) < value; });
        const auto cell = static_cast<std::uint16_t>(
            static_cast<unsigned>(index) | (edit == BlockEdit::Removed ? kRemovedFlag : 0U));
        if (!old) {
            cells_.insert(it, cell);
        } else if (!edit) {
            cells_.erase(it);
        } else {
            *it = cell;
        }
    }
    normalize();
}

std::optional<BlockEdit> BlockSection::uniform() const {
    if (removed_ == kSectionBlocks) {
        return BlockEdit::Removed;
    }
    if (added_ == kSectionBlocks) {
        return BlockEdit::Added;
    }
    return std::nullopt;
}

std::optional<std::pair<int, int>> BlockSection::editedLayers(int x0, int x1, int z0,
                                                              int z1) const {
    if (empty() || x0 > x1 || z0 > z1) {
        return std::nullopt;
    }
    if (isFilled()) {
        return std::pair{0, kSectionSide - 1};
    }
    std::optional<std::pair<int, int>> layers;
    const auto add = [&layers](int y) {
        layers = layers ? std::pair{std::min(layers->first, y), std::max(layers->second, y)}
                        : std::pair{y, y};
    };
    if (masks_.empty()) {
        for (const std::uint16_t cell : cells_) {
            const int index = cell & kIndexMask;
            const int x = index % kSectionSide;
            const int z = (index / kSectionSide) % kSectionSide;
            if (x >= x0 && x <= x1 && z >= z0 && z <= z1) {
                add(index / (kSectionSide * kSectionSide));
            }
        }
        return layers;
    }
    const std::uint16_t columns = rowMask(x0, x1);
    for (int y = 0; y < kSectionSide; ++y) {
        for (int z = z0; z <= z1; ++z) {
            const auto row = static_cast<std::uint16_t>(
                rowOf(masks_.data(), y, z) | rowOf(masks_.data() + kSectionWords, y, z));
            if ((row & columns) != 0) {
                add(y);
                break;
            }
        }
    }
    return layers;
}

void BlockSection::addColumns(SectionColumns& columns) const {
    if (isFilled()) {
        columns.fill(kRowBits);
        return;
    }
    forEach([&columns](int index, BlockEdit) {
        const int x = index % kSectionSide;
        const int z = (index / kSectionSide) % kSectionSide;
        columns[static_cast<std::size_t>(z)] |= static_cast<std::uint16_t>(1U << x);
    });
}

std::size_t BlockSection::heapBytes() const {
    return cells_.capacity() * sizeof(std::uint16_t) + masks_.capacity() * sizeof(std::uint64_t);
}

void BlockSection::toDense() {
    SectionMask removed{};
    SectionMask added{};
    if (removed_ + added_ > 0 && cells_.empty()) {
        // Filled before the change of a block: every other block keeps the edit.
        const bool wasRemoved = removed_ >= added_;
        (wasRemoved ? removed : added).fill(~std::uint64_t{0});
    } else {
        toMasks(removed, added);
    }
    masks_.assign(removed.begin(), removed.end());
    masks_.insert(masks_.end(), added.begin(), added.end());
    release(cells_);
}

void BlockSection::normalize() {
    const int edited = count();
    if (edited == 0 || isFilled()) {
        release(cells_);
        release(masks_);
        return;
    }
    if (edited <= kSparseLimit && !masks_.empty()) {
        std::vector<std::uint16_t> cells;
        cells.reserve(static_cast<std::size_t>(edited));
        forEach([&cells](int index, BlockEdit edit) {
            cells.push_back(static_cast<std::uint16_t>(
                static_cast<unsigned>(index) | (edit == BlockEdit::Removed ? kRemovedFlag : 0U)));
        });
        release(masks_);
        cells_ = std::move(cells);
    } else if (edited > kSparseLimit && masks_.empty()) {
        toDense();
    }
}

} // namespace terrain

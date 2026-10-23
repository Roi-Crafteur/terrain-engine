#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace terrain {

enum class BlockEdit : std::uint8_t {
    Added,   // density = min(density, cube): the cube becomes rock
    Removed, // density = max(density, -cube): the cube becomes air
};

// A section: the cube of 16 x 16 x 16 blocks of a chunk between the heights 16 n and 16 n + 15,
// n = sectionOf(y). Its blocks are indexed by their local coordinates in [0, 16), x fastest, then
// z, then y.
inline constexpr int kSectionShift = 4;
inline constexpr int kSectionSide = 1 << kSectionShift;
inline constexpr int kSectionBlocks = kSectionSide * kSectionSide * kSectionSide;
inline constexpr int kSectionWords = kSectionBlocks / 64;
// One bit per block of a section, by index.
using SectionMask = std::array<std::uint64_t, kSectionWords>;
// Bit x of columns[z]: the column (x, z) of a section.
using SectionColumns = std::array<std::uint16_t, kSectionSide>;

[[nodiscard]] constexpr std::int32_t sectionOf(std::int32_t y) {
    return y >> kSectionShift;
}
// Local coordinate of a block coordinate in its section or chunk.
[[nodiscard]] constexpr int localOf(std::int32_t v) {
    return v & (kSectionSide - 1);
}
[[nodiscard]] constexpr int sectionIndex(int x, int y, int z) {
    return (y * kSectionSide + z) * kSectionSide + x;
}

// The edits of the blocks of a section, each block generated, added or removed. Its form is a
// function of its blocks alone, so that equal sections compare equal: every block generated
// (empty), every block edited the same way (filled, nothing on the heap), up to kSparseLimit
// edited blocks (a sorted list) or more (two masks, 1 KiB).
class BlockSection {
public:
    static constexpr int kSparseLimit = 128;

    BlockSection() = default;
    [[nodiscard]] static BlockSection filled(BlockEdit edit);
    // The section whose removed and added blocks are the bits of the masks, which share none.
    [[nodiscard]] static BlockSection fromMasks(const SectionMask& removed,
                                                const SectionMask& added);
    void toMasks(SectionMask& removed, SectionMask& added) const;

    [[nodiscard]] std::optional<BlockEdit> at(int index) const;
    // nullopt gives the block back to the generated terrain.
    void set(int index, std::optional<BlockEdit> edit);

    [[nodiscard]] bool empty() const { return count() == 0; }
    // The edit of every block, if they all share one.
    [[nodiscard]] std::optional<BlockEdit> uniform() const;
    [[nodiscard]] int count() const { return removed_ + added_; }
    [[nodiscard]] int count(BlockEdit edit) const {
        return edit == BlockEdit::Removed ? removed_ : added_;
    }
    // Lowest and highest local y of an edited block in the columns [x0, x1] x [z0, z1] (local
    // coordinates), or nullopt.
    [[nodiscard]] std::optional<std::pair<int, int>> editedLayers(int x0, int x1, int z0,
                                                                  int z1) const;
    // Sets the bits of the columns holding an edited block.
    void addColumns(SectionColumns& columns) const;
    // Calls visit(index, edit) for every edited block, by increasing index.
    template <class Visit>
    void forEach(Visit visit) const;
    // Bytes the section holds on the heap.
    [[nodiscard]] std::size_t heapBytes() const;

    friend bool operator==(const BlockSection&, const BlockSection&) = default;

private:
    static constexpr std::uint16_t kRemovedFlag = 1U << 12;
    static constexpr std::uint16_t kIndexMask = kRemovedFlag - 1;

    [[nodiscard]] bool isFilled() const {
        return removed_ == kSectionBlocks || added_ == kSectionBlocks;
    }
    void toDense();
    void normalize();

    std::vector<std::uint16_t> cells_; // sparse: index | kRemovedFlag if removed, by index
    std::vector<std::uint64_t> masks_; // dense: the removed words, then the added words
    std::uint16_t removed_ = 0;
    std::uint16_t added_ = 0;
};

template <class Visit>
void BlockSection::forEach(Visit visit) const {
    if (isFilled()) {
        const BlockEdit edit = removed_ != 0 ? BlockEdit::Removed : BlockEdit::Added;
        for (int index = 0; index < kSectionBlocks; ++index) {
            visit(index, edit);
        }
    } else if (!masks_.empty()) {
        for (int w = 0; w < kSectionWords; ++w) {
            const auto word = static_cast<std::size_t>(w);
            std::uint64_t bits = masks_[word] | masks_[word + kSectionWords];
            while (bits != 0) {
                const int bit = std::countr_zero(bits);
                const bool removed = ((masks_[word] >> bit) & 1U) != 0;
                visit(w * 64 + bit, removed ? BlockEdit::Removed : BlockEdit::Added);
                bits &= bits - 1;
            }
        }
    } else {
        for (const std::uint16_t cell : cells_) {
            visit(static_cast<int>(cell & kIndexMask),
                  (cell & kRemovedFlag) != 0 ? BlockEdit::Removed : BlockEdit::Added);
        }
    }
}

} // namespace terrain

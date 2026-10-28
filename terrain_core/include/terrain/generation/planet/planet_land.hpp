#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

// Compact arrays of the planet level (phase 11D): the hydrology only has values on the land
// (37 % of the cells of the planet), and a few of them only on the cells of the depressions or
// of the lakes (a few %). Arrays on every cell would make the creation of the level need twice
// the memory of the game.
namespace terrain::planet {

// Frees the memory of a vector (v = {} would only empty it, its capacity kept).
template <class T> void freeVector(std::vector<T>& v) {
    std::vector<T>().swap(v);
}

// The land cells of a grid (kLand in an ocean mask), numbered in the order of the cells: a bit
// per cell and the count of land cells before each word of 64 bits, so that the number of a land
// cell among them (its slot) costs a popcount. 12 bytes per 64 cells.
class LandIndex {
public:
    LandIndex() = default;
    // The land of an ocean mask (OceanKind): its cells kLand.
    explicit LandIndex(const std::vector<std::uint8_t>& ocean);
    // The cells of a sorted list (each once) taken as the land: the numbering of any set of cells.
    [[nodiscard]] static LandIndex ofCells(std::size_t cells,
                                           const std::vector<std::uint32_t>& sorted);

    [[nodiscard]] std::size_t cells() const { return cells_; }
    [[nodiscard]] std::size_t landCells() const { return land_; }
    [[nodiscard]] bool land(std::size_t cell) const {
        return ((bits_[cell >> 6] >> (cell & 63U)) & 1U) != 0;
    }
    // The land cells before cell: the slot of a land cell.
    [[nodiscard]] std::size_t slot(std::size_t cell) const {
        const std::uint64_t below = (std::uint64_t{1} << (cell & 63U)) - 1U;
        return before_[cell >> 6] +
               static_cast<std::size_t>(std::popcount(bits_[cell >> 6] & below));
    }
    // The sea cells before cell: the slot of a sea cell among the sea cells.
    [[nodiscard]] std::size_t seaSlot(std::size_t cell) const { return cell - slot(cell); }

    // Calls f(cell, slot) for every land cell, in increasing order.
    template <class F> void forEachLand(F&& f) const {
        for (std::size_t word = 0; word < bits_.size(); ++word) {
            std::size_t slot = before_[word];
            for (std::uint64_t bits = bits_[word]; bits != 0; bits &= bits - 1U) {
                f(word * 64U + static_cast<std::size_t>(std::countr_zero(bits)), slot++);
            }
        }
    }

    [[nodiscard]] std::size_t memoryBytes() const {
        return bits_.capacity() * sizeof(std::uint64_t) +
               before_.capacity() * sizeof(std::uint32_t);
    }

private:
    std::vector<std::uint64_t> bits_;
    std::vector<std::uint32_t> before_;
    std::size_t cells_ = 0;
    std::size_t land_ = 0;
};

// A value per land cell, outside on the others (the sea), read by cell.
template <class T> class LandValues {
public:
    LandValues() = default;
    LandValues(std::shared_ptr<const LandIndex> index, T value, T outside)
        : index_(std::move(index)), values_(index_->landCells(), value), outside_(outside) {}
    // values: one per land cell, by slot.
    LandValues(std::shared_ptr<const LandIndex> index, std::vector<T> values, T outside)
        : index_(std::move(index)), values_(std::move(values)), outside_(outside) {}

    // The value of a cell: its own on land, outside at sea.
    [[nodiscard]] T operator[](std::size_t cell) const {
        return index_->land(cell) ? values_[index_->slot(cell)] : outside_;
    }
    // The value of a land cell, to change.
    [[nodiscard]] T& at(std::size_t cell) { return values_[index_->slot(cell)]; }

    // The values by slot.
    [[nodiscard]] std::vector<T>& values() { return values_; }
    [[nodiscard]] const std::vector<T>& values() const { return values_; }
    [[nodiscard]] const LandIndex& index() const { return *index_; }
    [[nodiscard]] const std::shared_ptr<const LandIndex>& sharedIndex() const { return index_; }
    [[nodiscard]] T outside() const { return outside_; }
    [[nodiscard]] bool empty() const { return values_.empty(); }
    // Frees the values.
    void release() { freeVector(values_); }

    // The value of every cell, the sea included.
    [[nodiscard]] std::vector<T> expand() const {
        std::vector<T> all(index_->cells(), outside_);
        index_->forEachLand([&](std::size_t cell, std::size_t slot) { all[cell] = values_[slot]; });
        return all;
    }

private:
    std::shared_ptr<const LandIndex> index_;
    std::vector<T> values_;
    T outside_{};
};

// A value on a few cells, outside on the others: those cells sorted with their values, and a bit
// per cell so that the others cost no search. Filled in any order by add, then sorted by finish.
template <class T> class SparseCells {
public:
    struct Entry {
        std::uint32_t cell;
        T value;
    };

    SparseCells() = default;
    SparseCells(std::size_t cells, T outside)
        : bits_((cells + 63U) / 64U, 0), cells_(cells), outside_(outside) {}

    // A cell that has no value yet.
    void add(std::size_t cell, T value) {
        bits_[cell >> 6] |= std::uint64_t{1} << (cell & 63U);
        entries_.push_back({static_cast<std::uint32_t>(cell), value});
    }
    // Sorts the cells added (each once): before any reading.
    void finish() {
        std::sort(entries_.begin(), entries_.end(),
                  [](const Entry& a, const Entry& b) { return a.cell < b.cell; });
        entries_.shrink_to_fit();
    }

    [[nodiscard]] bool has(std::size_t cell) const {
        return !bits_.empty() && ((bits_[cell >> 6] >> (cell & 63U)) & 1U) != 0;
    }
    [[nodiscard]] T operator[](std::size_t cell) const {
        if (!has(cell)) {
            return outside_;
        }
        const auto it = std::lower_bound(entries_.begin(), entries_.end(), cell,
                                         [](const Entry& e, std::size_t c) { return e.cell < c; });
        return it->value;
    }
    // The cells that have a value, by increasing cell.
    [[nodiscard]] const std::vector<Entry>& entries() const { return entries_; }
    [[nodiscard]] T outside() const { return outside_; }
    void release() {
        freeVector(bits_);
        freeVector(entries_);
    }

    // The value of every cell.
    [[nodiscard]] std::vector<T> expand() const {
        std::vector<T> all(cells_, outside_);
        for (const Entry& e : entries_) {
            all[e.cell] = e.value;
        }
        return all;
    }

private:
    std::vector<std::uint64_t> bits_;
    std::vector<Entry> entries_;
    std::size_t cells_ = 0;
    T outside_{};
};

} // namespace terrain::planet

#pragma once

#include "terrain/streaming/horizon_levels.hpp"
#include "terrain/streaming/horizon_sampler.hpp"

#include <optional>
#include <span>
#include <vector>

namespace terrain {

// One level of the far horizon: the window of (K + 1) x (K + 1) samples around its center, in a
// toroidal buffer of as many slots (slotOf), as a renderer keeps it in a texture. Moving the
// window by a few samples only replaces the slots of the samples that leave it.
//
// The center is a continuous index (moveCenter): it follows the observer across the edges of
// the wrapped world without jumping, so the slots of a sample depend on the path of the
// observer; the samples themselves never do. Read them by their index (at, find) or from the
// low corner of the window (atWindow).
class HorizonLevel {
public:
    // Throws std::invalid_argument for invalid parameters (validate) or a level out of range.
    HorizonLevel(const HorizonParams& params, int level);

    [[nodiscard]] int level() const { return level_; }
    [[nodiscard]] int pitch() const { return pitch_; }
    [[nodiscard]] int side() const { return side_; } // samples along a side of the window
    [[nodiscard]] HorizonIndex counts() const { return counts_; }

    // False until the window is placed (the first moveTo).
    [[nodiscard]] bool placed() const { return placed_; }
    [[nodiscard]] HorizonIndex center() const { return center_; }
    [[nodiscard]] HorizonRect window() const;

    // Moves the window: the samples entering it (newSamples) hold stale values until written.
    void moveTo(HorizonIndex center);
    // Writes the samples of a rectangle of the window, row by row (x varies fastest). Throws
    // std::invalid_argument for a rectangle outside the window or cells of another count.
    void write(const HorizonRect& rect, std::span<const HorizonCell> cells);

    // Sample of the window, by its index (continuous, as the window) or from its low corner.
    [[nodiscard]] const HorizonCell& at(HorizonIndex sample) const;
    [[nodiscard]] const HorizonCell& atWindow(int i, int k) const;
    // The image in the window of a sample given by any index (canonical for instance), or
    // nullptr outside the window.
    [[nodiscard]] const HorizonCell* find(HorizonIndex sample) const;

    // The toroidal buffer, side x side slots row by row, and the slot of the low corner of the
    // window: sample window().x0 + i lies in slot (firstSlot().x + i) mod side.
    [[nodiscard]] std::span<const HorizonCell> slots() const { return slots_; }
    [[nodiscard]] HorizonIndex firstSlot() const;

    // World position of the low corner of the window, at the image nearest to the observer
    // (meters): sample window().x0 + i lies at originX(x) + i * pitch.
    [[nodiscard]] double originX(double observerX) const;
    [[nodiscard]] double originZ(double observerZ) const;

    // Ground at (x, z), any image, bilinear between the 4 samples around it, or nullopt
    // outside the window: the surface a renderer draws from this level (the target of the
    // geomorphing of the next finer level along its border).
    [[nodiscard]] std::optional<float> heightAt(double x, double z) const;

    // Bytes held by the samples.
    [[nodiscard]] std::size_t memoryBytes() const { return slots_.size() * sizeof(HorizonCell); }

private:
    [[nodiscard]] std::size_t slotIndex(HorizonIndex sample) const;

    int level_;
    int pitch_;
    int side_;
    HorizonIndex counts_;
    WorldSize world_;
    bool placed_ = false;
    HorizonIndex center_;
    std::vector<HorizonCell> slots_;
};

} // namespace terrain

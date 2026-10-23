#pragma once

// The blocks around a lattice point and their cubes, shared by the edit windows and lattices
// (edit_window.cpp, edit_lattice.cpp) only.

#include "terrain/storage/modifications.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace terrain::edit_cubes {

static_assert(kBlockReach == 1, "the edits read the 3 x 3 x 3 blocks around a lattice point");

inline constexpr std::uint8_t kRemovedState = 1;
inline constexpr std::uint8_t kAddedState = 2;

// Index of the block at the offset (dx, dy, dz) of a lattice point, each in [-1, 1].
constexpr std::size_t offsetIndex(int dx, int dy, int dz) {
    return static_cast<std::size_t>(((dz + 1) * 3 + (dy + 1)) * 3 + (dx + 1));
}

// Signed distances from a lattice point to the cubes of the blocks around it (blockDistance).
inline std::array<float, 27> makeCubeDistances() {
    std::array<float, 27> d{};
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                d[offsetIndex(dx, dy, dz)] = blockDistance({0, 0, 0}, dx, dy, dz);
            }
        }
    }
    return d;
}

inline const std::array<float, 27> kCubeDistances = makeCubeDistances();

// The blocks around a lattice point by the number of their offsets that are not 0: the center,
// the 6 faces, the 12 edges and the 8 corners, each class at one distance (kCubeDistances).
inline constexpr std::array<std::uint32_t, 4> kOffsetClasses = [] {
    std::array<std::uint32_t, 4> classes{};
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int moved = (dx != 0 ? 1 : 0) + (dy != 0 ? 1 : 0) + (dz != 0 ? 1 : 0);
                classes[static_cast<std::size_t>(moved)] |= 1U << offsetIndex(dx, dy, dz);
            }
        }
    }
    return classes;
}();

// The distance to the nearest cube among the blocks of the bits (offsetIndex), not all 0: the
// minimum of the distances of the blocks, the classes being sorted by distance.
inline float nearestCube(std::uint32_t blocks) {
    constexpr std::array<std::size_t, 4> kFirst = {offsetIndex(0, 0, 0), offsetIndex(1, 0, 0),
                                                   offsetIndex(1, 1, 0), offsetIndex(1, 1, 1)};
    for (std::size_t c = 0; c < 3; ++c) {
        if ((blocks & kOffsetClasses[c]) != 0) {
            return kCubeDistances[kFirst[c]];
        }
    }
    return kCubeDistances[kFirst[3]];
}

// The nearest cubes of the added and of the removed blocks around a lattice point. Every added
// block first (min), then every removed one (max), as editDensity block by block: min and max
// are exact, so their order never changes a bit.
class NearestCubes {
public:
    void add(std::uint8_t state, std::size_t offset) {
        if (state == 0) {
            return;
        }
        const float cube = kCubeDistances[offset];
        if (state == kAddedState) {
            added_ = anyAdded_ ? std::min(added_, cube) : cube;
            anyAdded_ = true;
        } else {
            removed_ = anyRemoved_ ? std::min(removed_, cube) : cube;
            anyRemoved_ = true;
        }
    }
    [[nodiscard]] float apply(float density) const {
        if (anyAdded_) {
            density = std::min(density, added_);
        }
        if (anyRemoved_) {
            density = std::max(density, -removed_);
        }
        return density;
    }

private:
    float added_ = 0.0f;
    float removed_ = 0.0f;
    bool anyAdded_ = false;
    bool anyRemoved_ = false;
};

inline std::uint8_t stateOf(std::optional<BlockEdit> edit) {
    if (!edit) {
        return 0;
    }
    return *edit == BlockEdit::Removed ? kRemovedState : kAddedState;
}

} // namespace terrain::edit_cubes

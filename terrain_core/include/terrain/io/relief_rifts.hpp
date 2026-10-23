#pragma once

// The continental rifts of the planetary geography, for the checks of the relief (phase 11R
// bis): where their troughs lie and what a profile across them measures.

#include "terrain/generation/heightfield.hpp"

#include <vector>

namespace terrain {

// A rift measured across its middle: the heights every kRiftStep meters over +-kRiftSpan from
// its center, across its trough.
struct RiftProfile {
    int nodes = 0;   // nodes of the map in its trough
    double x = 0.0;  // its center, a point of the world on its axis
    double z = 0.0;
    double acrossX = 1.0; // unit vector across the trough
    double acrossZ = 0.0;
    float floor = 0.0f;   // median height within 10 km of the center
    float rimLow = 0.0f;  // the highest point on the lower side
    float rimHigh = 0.0f; // and on the other
    double width = 0.0;   // between the two highest points
    double wall = 0.0;    // from the center to the foot of its wall (kRiftRise above the floor)
    std::vector<float> heights;

    [[nodiscard]] double depth() const {
        return static_cast<double>(rimLow) - static_cast<double>(floor);
    }
};

inline constexpr double kRiftStep = 500.0;
inline constexpr double kRiftSpan = 300'000.0;
inline constexpr float kRiftRise = 150.0f;

// The rifts of the geography of field, largest first (components of the nodes of its map whose
// trough is at least 0.9 on land), at most count of them, each measured at its middle node,
// across the direction in which its trough falls fastest. Empty without a geography.
[[nodiscard]] std::vector<RiftProfile> measureRifts(const Heightfield& field, int count);

} // namespace terrain

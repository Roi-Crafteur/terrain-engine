#pragma once

#include <array>

// Internal to the generation sources: the 8 neighbors of a grid cell.
namespace terrain::grid {

struct Neighbor {
    int dx;
    int dz;
    float distance; // in cells
};

inline constexpr float kDiagonal = 1.41421356f;

// Fixed order, so that ties between neighbors are always broken the same way.
inline constexpr std::array<Neighbor, 8> kNeighbors = {{
    {1, 0, 1.0f},
    {-1, 0, 1.0f},
    {0, 1, 1.0f},
    {0, -1, 1.0f},
    {1, 1, kDiagonal},
    {-1, 1, kDiagonal},
    {1, -1, kDiagonal},
    {-1, -1, kDiagonal},
}};

} // namespace terrain::grid

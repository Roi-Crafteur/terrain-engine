#pragma once

// Tributary valleys of the mountains of the planetary geography (phase 11R), internal to
// terrain_core: between the valleys of the drainage of the map (Geography::valleyShape), they
// furrow the mountains down their regional slope.
//
// Each octave is a pattern of valleys about spacing apart, running down a direction (phacelle
// noise): the four anchors of the cells around a point each draw straight parallel valleys
// through themselves, the direction turned by up to about 35 degrees per anchor and their spacing
// up to 30 % closer or farther, a periodic profile of the distance across (straight or concave
// walls, sharp ridges); their blend bends the valleys (sinuous), keeps them continuous where the
// anchors agree and forks or ends them where they do not (branches). The direction of the
// largest octave is the regional slope; that of each next octave also goes down the walls of the
// valleys of the one before. Some anchors have none, the depth of the others varies: no regular
// furrows. None of them goes down to the floors of the valleys of the drainage (they may end,
// where the anchors disagree). Valleys fade out where the regional slope vanishes (plateaus,
// divides). Pure arithmetic of doubles and hashes: the same bits on every platform; periodic on
// the world.

#include "terrain/world_size.hpp"

#include <array>
#include <cstdint>

namespace terrain::valleys {

struct Octave {
    std::uint64_t seed = 0;
    double spacing = 1.0; // meters between valleys, a divisor of the sides of the world
    int cellsX = 1;       // anchor cells along each side of the world
    int cellsZ = 1;
    float depth = 1.0f; // of the valleys on their axis, as a share of the mountains
};

struct Network {
    std::array<Octave, 2> octaves;
    WorldSize world;
};

// Spacings of 32,768 and 16,384 m: whole numbers of them along every side of a world.
[[nodiscard]] Network makeNetwork(std::uint64_t seed, const WorldSize& world);

// The share of the mountains left at (x, z) of the canonical range: 1 on the ridges, down to
// 0.4 on the axis of the largest valleys. (downX, downZ) is the direction down the regional
// elevation, of norm slope (meters per meter).
[[nodiscard]] float carve(const Network& network, double x, double z, double downX, double downZ,
                          double slope);

} // namespace terrain::valleys

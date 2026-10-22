#pragma once

// The ravines of the mountain slopes of the planetary geography (phase 11R bis), internal to
// terrain_core: V-shaped gullies running down the local slope of the mountains, with sharp
// spurs between them, about 2 km and 1 km apart, as deep as the slope is steep (a share of the
// drop of the slope over their spacing): deep side valleys on the steep walls, shallow furrows on
// gentle slopes, none on the flat.
//
// Each octave is a pattern of ravines running down a direction (phacelle noise): the four
// anchors of the cells around a point each draw straight parallel ravines through themselves,
// the direction turned by up to about 25 degrees per anchor and their spacing up to 35 % closer
// or farther; their blend bends the ravines, keeps them continuous where the anchors agree and
// forks or ends them where they do not. A third of the anchors have none and the depth of the
// others varies: the ravines begin, fork and end at various lengths, and no comb or grid shows.
// The direction of the first octave is the slope given by the caller; that of the second also
// turns down the walls of the ravines of the first (tributaries). Pure arithmetic of doubles and
// hashes: the same bits on every platform; periodic on the world when the caller's points are.

#include "terrain/world_size.hpp"

#include <array>
#include <cstdint>

namespace terrain::ravines {

struct Octave {
    std::uint64_t seed = 0;
    double spacing = 1.0; // meters between ravines, a divisor of the sides of the world
    int cellsX = 1;       // anchor cells along each side of the world
    int cellsZ = 1;
    float depth = 1.0f; // of the ravines on their axis, as a share of the drop over the spacing
};

struct Network {
    std::array<Octave, 2> octaves;
    WorldSize world;
};

// The anchors of the last cell met by each octave during a batch call (their hashes, and the
// inverse norm of the directions they turn): a point in the same cell reuses them, with the same
// bits.
struct Memo {
    struct Cell {
        bool valid = false;
        double fx = 0.0;
        double fz = 0.0;
        std::array<std::uint64_t, 4> hashes{};
        std::array<double, 4> inverse{};
    };
    std::array<Cell, 2> octaves{};
};

// Spacings of 2,048 and 1,024 m: whole numbers of them along every side of a world.
[[nodiscard]] Network makeNetwork(std::uint64_t seed, const WorldSize& world);

// The depth of the ravines at (x, z), meters, any point (the anchors wrap on the world): 0 on
// the spurs, the most on the axis of the deepest ravines. (slopeX, slopeZ) is the gradient of
// the slope (meters per meter, up the slope): the ravines run down it, as deep as it is steep.
// strength in [0, 1] scales them (0: none, the result is exactly 0). memo may be null.
[[nodiscard]] float depth(const Network& network, double x, double z, double slopeX, double slopeZ,
                          double strength, Memo* memo = nullptr);

} // namespace terrain::ravines

#pragma once

// The ravines of the mountain slopes of the planetary geography (phase 11R bis), internal to
// terrain_core: V-shaped gullies running down the local slope of the mountains, with sharp
// spurs between them, about 2 km and 1 km apart, as deep as the slope is steep (a share of the
// drop of the slope over their spacing): deep side valleys on the steep walls, shallow furrows on
// gentle slopes, none on the flat.
//
// Each octave is a scatter of ravines: the anchor of each cell (a third of a cell from its
// center at most) draws one ravine through itself, straight down the slope at the point, of its
// own length, width and depth; a fifth of the anchors have none. A ravine deepens from its head
// and fades out at its mouth, and where ravines overlap the deepest is kept: the spurs between
// them are sharp, and no blend of phases twists or dams them. The direction of the first octave
// is the slope given by the caller; that of the second also turns down the walls of the ravines
// of the first (tributaries). Pure arithmetic of doubles and hashes: the same bits on every
// platform; periodic on the world when the caller's points are.

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

// The anchors around the last cell met by each octave during a batch call (their place, length,
// width and depth): a point nearest to the same cell reuses them, with the same bits.
struct Memo {
    struct Anchor {
        double x = 0.0; // cells from the center of the cell
        double z = 0.0;
        double length = 0.0; // cells on each side of the anchor, down the slope
        double width = 0.0;  // cells on each side of the axis
        double depth = 0.0;  // share of the depth of the octave, 0 without a ravine
    };
    struct Cell {
        bool valid = false;
        double i = 0.0;
        double j = 0.0;
        std::array<Anchor, 9> anchors{};
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

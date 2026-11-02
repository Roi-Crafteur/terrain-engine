#pragma once

// The inside of the ergs (ergs.hpp), internal to terrain_core: the field of blocks of the planet
// level (erg_field.cpp), the noises of the mask and of the dunes (ergs.cpp, dune_field.cpp).

#include "terrain/generation/ergs.hpp"
#include "terrain/generation/noise_octaves.hpp"
#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/planet/planet_params.hpp"
#include "terrain/world_size.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace terrain {

namespace erg_field {

// Blocks of blockCells x blockCells cells of the planet level (8, or the largest power of two
// below it that divides both sides of the grid), each with the potential of its climate and the
// water of its cells.
inline constexpr int kMostBlockCells = 8;

struct Field {
    PlanetGrid grid;
    int blockCells = 1;
    int bx = 0; // blocks along x and z
    int bz = 0;
    double blockSize = 0.0;
    std::vector<float> potential;      // per block, [0, 1]: climate x distance to the sea x land
    std::vector<std::uint8_t> reach;   // per block: an erg may lie within reach of it
    std::vector<std::uint64_t> water;  // per block: bit (i % b) + 8 (j % b) of a water cell
    std::vector<std::uint64_t> lakes;  // per block: the same bits, of the lake cells among them
    // The receivers (kPlanetNeighbors index, or kNoNeighbor) of the river cells (water, not lake),
    // block by block in the order of their bits, from riverStart[block].
    std::vector<std::uint32_t> riverStart;
    std::vector<std::uint8_t> receivers;

    [[nodiscard]] std::size_t block(int bi, int bj) const {
        return static_cast<std::size_t>(bj) * static_cast<std::size_t>(bx) +
               static_cast<std::size_t>(bi);
    }
    // What cell (i, j) of the planet level (wrapped) holds: none, a lake, or a river of at least
    // ErgParams::minDischarge with the direction of its receiver.
    struct Water {
        bool any = false;
        bool lake = false;
        std::uint8_t receiver = kNoNeighbor;
    };
    [[nodiscard]] Water waterCell(int i, int j) const;
};

// The field of a planet level. warpReach: the farthest the warp moves a point, meters (the reach
// of the blocks is dilated by it).
[[nodiscard]] Field build(const PlanetLevel& planet, const ErgParams& params, double warpReach);

// The potential at the canonical (x, z): the blocks interpolated between their centers with
// smoothstep weights (continuous, with a continuous slope), on the torus.
[[nodiscard]] float potentialAt(const Field& field, double x, double z);
// The reach of the block holding the canonical (x, z).
[[nodiscard]] bool reachAt(const Field& field, double x, double z);
// The distance from the canonical (x, z) to the nearest water, meters, or limit if none is nearer:
// the square of a lake cell, the course of a river (from the center of its cell to that of its
// receiver).
[[nodiscard]] double waterDistance(const Field& field, double x, double z, double limit);

} // namespace erg_field

struct Ergs::Impl {
    std::uint64_t seed = 0;
    ErgParams params;
    PlanetClimateParams climate;
    WorldSize world;
    erg_field::Field field;
    const PlanetLevel* planet = nullptr; // read for the humidity at a point: outlives the ergs
    std::vector<Depression> depressions;
    double degree = 0.0; // meters of one degree of latitude
    // The mask: the warp of the field, the fraying of its edges, the sand, the variability and the
    // turn of the wind.
    noise::FbmOctaves warpX;
    noise::FbmOctaves warpZ;
    noise::FbmOctaves edges;
    noise::FbmOctaves supply;
    noise::FbmOctaves sand;
    noise::FbmOctaves variability;
    noise::FbmOctaves turn;
    // The dunes (dune_field.cpp): the sinuosity of the crests, the drift of their phase, the
    // height along them, the side of the slip faces of the seifs and their ends.
    noise::FbmOctaves sinuosity;
    noise::FbmOctaves drift;
    noise::FbmOctaves amplitude;
    noise::FbmOctaves side;
    noise::FbmOctaves ends;
};

// The prevailing wind at the canonical (x, z) (Ergs::windAt).
[[nodiscard]] std::array<double, 2> windOf(const Ergs::Impl& m, double x, double z);

namespace dune_field {

// The weight of each kind of dunes at an erg point, summing to 1: the stars where the winds come
// from every side, the seifs where they come from two, else barchans where the sand is scarce,
// transverse ridges, and draas where it is plentiful.
struct FormWeights {
    float barchan = 0.0f;
    float transverse = 0.0f;
    float draa = 0.0f;
    float seif = 0.0f;
    float star = 0.0f;
};
[[nodiscard]] FormWeights weightsOf(const ErgPoint& e);
// The height of the dunes of a sand in [0, 1] and a noise of the heights about [-1, 1]: 20 to
// 300 m.
[[nodiscard]] double heightOf(double sand, double amplitude);

// The dunes at the canonical (x, z) of an erg point (dune_field.cpp).
[[nodiscard]] float dunes(const Ergs::Impl& ergs, double x, double z, const ErgPoint& e);

} // namespace dune_field

} // namespace terrain

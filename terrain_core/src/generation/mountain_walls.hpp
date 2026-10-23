#pragma once

// The walls of the mountains of the planetary geography (phase 11R bis), internal to
// terrain_core: rock walls of 10 to 500 m on the steep faces of the cores of the chains (and in
// rare isolated regions), built like the cliffs of the regions (CliffField): a union of vertical
// prisms in the rock, each with its own top, whose walls are the cliffs. Two characters, from
// region to region:
//   - strata (Dolomites, Grand Canyon): flat-topped prisms stacked every band of height, their
//     outlines the contour lines of the ground, receded by a noise: horizontal bands and steps.
//     Bands of 80 to 400 m (great walls) and, within them, of 12 to 60 m (rock steps); their
//     heights and offsets vary slowly (a gentle dip);
//   - crystalline (granite): the rock of each cell of a jittered lattice (its fractures) is cut
//     by joints along planes tilted 20 to 50 degrees in a direction of its own: oblique walls
//     and slabs, dihedrals where the cells meet, without horizontal layers. Cells of 512 m with
//     joints 60 to 220 m apart, and of 128 m with joints 15 to 55 m apart.
// The faces rise over a few meters (60 to 88 degrees) and never overhang: the rock has one
// surface, the top of the union of the prisms, computed in closed form. Its density is the
// vertical distance to that surface. The floors of the valleys, the gentle ground (passes) and
// the crests keep no wall. Pure arithmetic of doubles, noises and hashes: the same bits on every
// platform, periodic on the world.

#include "terrain/generation/noise_octaves.hpp"
#include "terrain/world_size.hpp"

#include <array>
#include <cstdint>

namespace terrain::mountain_walls {

struct Noises {
    noise::FbmOctaves character; // strata or crystalline, from region to region
    noise::FbmOctaves isolated;  // the rare isolated regions of walls
    noise::FbmOctaves patches;   // the walls come in patches on the faces
    noise::FbmOctaves bands;     // the height of the strata
    noise::FbmOctaves dip;       // their offset
    noise::FbmOctaves rims;      // the recession of their rims and the run of their faces
    std::uint64_t crystal = 0;   // the cells of the crystalline rock
    std::uint64_t layers = 0;    // which great bands are hard
    std::uint64_t steps = 0;     // and which rock steps
    WorldSize world;
    // The crystalline cells of each scale: their seed and how many along each side of the world.
    std::array<std::uint64_t, 2> cellSeeds{};
    std::array<int, 2> cellsX{1, 1};
    std::array<int, 2> cellsZ{1, 1};
};

[[nodiscard]] Noises makeNoises(std::uint64_t seed, const WorldSize& world);

// The cells of the crystalline rock around the last cell met at each scale. Their joints are only
// drawn (detailed) for the cells that cut the rock at a point: most of them only lie farther.
struct Cell {
    double x = 0.0; // in cells from the corner of the cell met
    double z = 0.0;
    std::uint64_t hash = 0; // of the cell, from which everything below is drawn
    bool detailed = false;  // the fields below are drawn
    double tiltX = 0.0; // the slope of its joints (meters per meter)
    double tiltZ = 0.0;
    double band = 1.0;   // meters between its joints
    double offset = 0.0; // of its first joint
    std::uint64_t layers = 0; // which of its joints are hard
};
struct Cells {
    bool valid = false;
    int i = 0;
    int j = 0;
    std::array<Cell, 9> cells{};
};

struct Memo {
    noise::FbmOctaves::Memo character;
    noise::FbmOctaves::Memo isolated;
    noise::FbmOctaves::Memo patches;
    noise::FbmOctaves::Memo bands;
    noise::FbmOctaves::Memo dip;
    noise::FbmOctaves::Memo rims;
    std::array<Cells, 2> crystal{};
};

// Everything the walls of a column depend on, apart from y.
struct Column {
    double ground = 0.0;  // meters, without walls
    double gradX = 0.0;   // the gradient of the mountains (meters per meter, up)
    double gradZ = 0.0;
    float intensity = 0.0f; // of the relief
    float character = 0.5f; // of the map: worn (0) to young and sharp (1)
    float floor = 0.0f;     // [0, 1]: on the floor of a valley (no wall)
    float crest = 0.0f;     // [0, 1]: on a crest (no wall: no fin, no needle)
    float relief = 0.0f;    // meters of the great forms above the base: the walls are on the
                            // great faces, not on the knolls of the floors
};

// The masks of the two characters at a column, in [0, 1], their sum at most 1, and the noise of
// the patches of walls there (about -1 to 1).
struct Masks {
    double strata = 0.0;
    double crystal = 0.0;
    double patch = 0.0;
};
[[nodiscard]] Masks masks(const Noises& noises, const Column& column, double x, double z,
                          Memo* memo);

// How the layers of a stack are drawn: the seed of the draws of each layer; the share of them
// that are hard here (a share that varies from place to place: banks that thin out and stop);
// the tread of a hard layer rising tilt of its band, plus up to spread of the rest (walls of
// uneven heights); the share scree of its wall buried under its scree.
struct Bedding {
    std::uint64_t layers = 0;
    double hard = 0.7;
    double tilt = 0.25;
    double spread = 0.0;
    double scree = 0.0;
};

// The top of a stack of strata over ground of slope slope (meters per meter): bands band meters
// high from offset, their rims receded by rim meters. A hard layer (blended over the draw of its
// hardness) is a ledge, then a face rising over run meters (0: a vertical wall) up to the next
// band, its foot buried in a concave apron of scree at most as steep as the scree at rest; a
// soft one keeps the slope. Continuous, rising with the ground, at most a few hundredths of a
// band above it.
[[nodiscard]] double strataTop(double ground, double slope, double band, double offset,
                               double rim, double run, const Bedding& bedding);

// The top of the crystalline rock over ground at (x, z) for the scale (0: cells of 512 m, 1: of
// 128 m), the ground of gradient (gradX, gradZ): the ground cut by the joints of each cell
// (strataTop along its tilted planes, waving with rims, the noise of the rims there, hard of
// them hard), blended
// with those of the neighbors within ramp meters of the fractures (0: the nearest cell alone).
[[nodiscard]] double crystalTop(const Noises& noises, double x, double z, double ground,
                                double gradX, double gradZ, int scale, double ramp, double rims,
                                double hard, Memo* memo);

// The top of the rock at a column at (x, z), canonical.
[[nodiscard]] double surface(const Noises& noises, const Column& column, double x, double z,
                             Memo* memo);

// The top of the rock of surface over ground of gradient (gradX, gradZ) with the masks given
// instead of those of the map: walls cut into the ground by other forms (the gorges of the rivers,
// phase 11B). surface is rockTop with masks (where they are not 0).
[[nodiscard]] double rockTop(const Noises& noises, const Masks& masks, double x, double z,
                             double ground, double gradX, double gradZ, Memo* memo);
// The share of strata of the walls at (x, z) for a character of the map (the rest crystalline),
// as surface draws it, without its isolated regions and its crystalline cores.
[[nodiscard]] double strataShare(const Noises& noises, double x, double z, float character,
                                 Memo* memo);

// The density of the rock at height y: the vertical distance to its surface (negative inside).
[[nodiscard]] double density(const Noises& noises, const Column& column, double x, double z,
                             double y, Memo* memo);

} // namespace terrain::mountain_walls

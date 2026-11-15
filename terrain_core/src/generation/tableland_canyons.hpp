#pragma once

// The canyons of the plateaus (agent 46, tableland_relief.hpp), internal to terrain_core: the zero
// lines of a meandering noise and their hanging branches, deepest at the escarpment, where their
// floor meets the low ground, rising inland. Their section is built of units, from the rim to the
// axis (the Grand Canyon, Canyonlands):
//   - a caprock: a cliff of 40 to 120 m under the rim;
//   - upper slopes, stepped by the strata;
//   - in the deep ones (from about 600 m), a great cliff of 120 to 200 m (the Redwall), a platform
//     of 0.3 to 1.5 km (the Tonto, the White Rim), and an inner gorge down to the floor, its width
//     and its slope varying along the axis (a noise of 4 km): no canal of parallel walls;
//   - a flat floor.
// On their platforms stand temples (tableland_temples.hpp): isolated buttes of two storeys, a talus
// all around, never over the inner gorge, so the floor stays drained. Each column reports the hard
// layer of its wall (the caprock, the great cliff, the caprock of a temple), which the rock lays as
// the forced hard layers (tableland_strata.cpp). Pure arithmetic of doubles and noises: the same
// bits on every platform, periodic on the world.

#include "tableland_cliffs.hpp"
#include "tableland_fields.hpp"
#include "tableland_temples.hpp"

#include "terrain/generation/noise_octaves.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>

namespace terrain::tableland_relief {

struct CanyonNoises {
    noise::FbmOctaves lines;      // the axes of the canyons
    noise::FbmOctaves branches;   // of their branches
    noise::FbmOctaves meanderX;   // the meanders of their branches (a warp of their axes)
    noise::FbmOctaves meanderZ;
    noise::FbmOctaves meanders;   // the entrenched meanders of the main ones (a shift of their axes)
    noise::FbmOctaves wiggles;    // and their bends of a kilometer or so (of their inner parts)
    noise::FbmOctaves mask;       // where they run
    noise::FbmOctaves great;      // the height of their great cliff
    noise::FbmOctaves platform;   // the width of their platform
    noise::FbmOctaves gorge;      // the width of their inner gorge along their axis
    TempleNoises temples;         // the temples on their platforms
    WorldSize world;
};

[[nodiscard]] CanyonNoises makeCanyonNoises(std::uint64_t seed, const WorldSize& world);

// The cut of the canyons at a column.
struct Cut {
    double axis = 0.0;  // meters from the axis of the nearest canyon (signed: its side)
    double gorge = 0.0; // meters from the edge of its inner gorge, positive within
    double depth = 0.0; // meters under the top of the plateau
    double gradX = 0.0; // of the depth
    double gradZ = 0.0;
    double floor = 0.0;  // [0, 1]: on the floor (no strata)
    double slope = 0.0;  // of the segment of the section at the column, meters per meter
    Unit unit;           // the hard layer of the wall there
    double bench = 0.0;  // meters under the rim of the top of its great cliff (0: none)
    bool cliff = false;  // on a cliff (its caprock, its great cliff, that of a temple)
    bool temple = false; // a temple stands there
    double templeOutline = 0.0; // meters to the rim of the lower storey of that temple, positive within
};

// What the walls of the canyons take of the plateau at a column: its style (0 humid, 1 arid), how
// far its rims recede (meters: the wide jag of the rims, canyonJagAt), the share of the height of a storey of a
// temple its caprock takes, the batter of the cliffs and their rough outline (tableland_cliffs.hpp:
// that of the temples).
struct Rims {
    double arid = 0.0;
    double jag = 0.0;
    double cliff = 0.6;
    double steep = 12.0;
    const CliffNoises* walls = nullptr;
    const noise::FbmOctaves* jags = nullptr; // the noise of that jag, read elsewhere (canyonJagAt)
};

// The jag of the rims of the canyons at (x, z), of the noise jags (tableland_relief.cpp): the wide
// one of the rims of the plateaus (its fine one would squeeze the walls of the canyons into cliffs
// where it turns fast).
[[nodiscard]] double canyonJagAt(const noise::FbmOctaves& jags, double x, double z);

// The canyons at (x, z) of a plateau thickness meters thick, inland in [0, 1] from its escarpment
// to its heart, scarp its escarpment, rims its rims.
[[nodiscard]] Cut canyonsAt(const CanyonNoises& n, double x, double z, double thickness,
                            double inland, const Field& scarp, const Rims& rims);

} // namespace terrain::tableland_relief

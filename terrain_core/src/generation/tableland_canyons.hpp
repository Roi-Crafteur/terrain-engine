#pragma once

// The canyons of the plateaus (agent 46, tableland_relief.hpp), internal to terrain_core: the zero
// lines of a meandering noise and their hanging branches, deepest at the escarpment, where their
// floor meets the low ground, rising inland. Their section is built of units, from the rim to the
// axis (the Grand Canyon, Canyonlands):
//   - a caprock: a cliff of 40 to 120 m under the rim;
//   - upper slopes, stepped by the strata;
//   - in the deep ones (from about 600 m), a great cliff of 120 to 200 m (the Redwall), a platform
//     of 0.3 to 1.5 km (the Tonto, the White Rim), and an inner gorge down to the floor;
//   - a flat floor.
// On their platforms and lower slopes stand temples: buttes of a noise over a threshold, their tops
// 10 to 30 % of the depth under the rim, a caprock over a talus; never over the inner gorge, so the
// floor stays drained. Each column reports the hard layer of its wall (the caprock, the great
// cliff, the caprock of a temple), which the rock lays as one forced layer
// (tableland_strata.cpp). Pure arithmetic of doubles and noises: the same bits on every platform,
// periodic on the world.

#include "tableland_fields.hpp"

#include "terrain/generation/noise_octaves.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>

namespace terrain::tableland_relief {

struct CanyonNoises {
    noise::FbmOctaves lines;      // the axes of the canyons
    noise::FbmOctaves branches;   // of their branches
    noise::FbmOctaves meanderX;   // their entrenched meanders (a warp of their axes)
    noise::FbmOctaves meanderZ;
    noise::FbmOctaves mask;       // where they run
    noise::FbmOctaves great;      // the height of their great cliff
    noise::FbmOctaves platform;   // the width of their platform
    noise::FbmOctaves temples;    // the temples on their platforms
    noise::FbmOctaves templeDrop; // how far under the rim the temples stand
    WorldSize world;
};

[[nodiscard]] CanyonNoises makeCanyonNoises(std::uint64_t seed, const WorldSize& world);

// A layer of hard rock forced into a wall at a column: its top in meters under the top of the
// plateau, its height; a caprock holds up to the top, the great cliff gives way above its top.
struct Unit {
    double top = 0.0;
    double height = 0.0; // 0: none
    bool capped = true;
};

// The cut of the canyons at a column.
struct Cut {
    double depth = 0.0; // meters under the top of the plateau
    double gradX = 0.0; // of the depth
    double gradZ = 0.0;
    double floor = 0.0;  // [0, 1]: on the floor (no strata)
    double slope = 0.0;  // of the segment of the section at the column, meters per meter
    Unit unit;           // the hard layer of the wall there
    bool cliff = false;  // on a cliff (its caprock, its great cliff, that of a temple)
    bool temple = false; // a temple stands there
};

// The canyons at (x, z) of a plateau thickness meters thick, scarp its escarpment, of style arid,
// their rims receding by jag meters, cliff the share of the height of a temple its caprock takes.
[[nodiscard]] Cut canyonsAt(const CanyonNoises& n, double x, double z, double thickness,
                            const Field& scarp, double arid, double jag, double cliff);

} // namespace terrain::tableland_relief

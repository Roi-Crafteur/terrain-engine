#pragma once

// The cliffs of the plateaus (agent 46, tableland_relief.hpp), internal to terrain_core: what makes
// a caprock a wall of rock and not a cookie cut (Merrick Butte, not Devils Tower):
//   - its outline rough from 20 to 200 m (alcoves, buttresses, re-entrants), added to the jag of
//     the rims (roughAt);
//   - its batter, 80 to 88 degrees by a noise (steepAt): the slope of the ground its hard layers
//     are laid over, so that their faces, vertical each, step back with the height;
//   - 2 to 4 hard layers in its height (layersAt), their count drifting from butte to butte: 1 to
//     3 ledges or setbacks, at heights of their own;
//   - scree on the talus under it (screeAt): blocks of a jittered lattice of about 30 m, opened by
//     a noise and drawn in size (no row of them), and cones of scree at rest (kScreeSlope) from the
//     foot of the cliff under its re-entrants;
//   - the vertical joints of its faces opened in clusters by a noise, their depths drawn
//     (jointsAt): irregular flutes, or none.
// Pure arithmetic of doubles, noises and hashes: the same bits on every platform, periodic on the
// world.

#include "tableland_fields.hpp"

#include "terrain/generation/noise_octaves.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>

namespace terrain::tableland_relief {

struct CliffNoises {
    noise::FbmOctaves rough;  // the outline, 160 to 40 m
    noise::FbmOctaves grit;   // and 40 to 20 m
    noise::FbmOctaves steep;  // the batter
    noise::FbmOctaves layers; // how many hard layers a caprock holds
    noise::FbmOctaves blocks; // where the blocks of scree lie
    noise::FbmOctaves joints; // where the joints of the faces open
    std::uint64_t blockSeed = 0;
    std::int64_t cellsX = 1; // of the lattice of the blocks over the world
    std::int64_t cellsZ = 1;
    WorldSize world;
};

[[nodiscard]] CliffNoises makeCliffNoises(std::uint64_t seed, const WorldSize& world);

// Meters the rough outline of the cliffs juts out (positive) or recedes at the canonical (x, z).
[[nodiscard]] double roughAt(const CliffNoises& n, double x, double z);

// The slope of the cliffs there, meters per meter: tan 80 to tan 88 degrees.
[[nodiscard]] double steepAt(const CliffNoises& n, double x, double z);

// How many hard layers a caprock holds there over its height: 2 to 4, not a whole number (the
// lowest one partial, its top laid on the bounds of the stack: tableland_strata.cpp), so the ledges
// stand at fractions of its height that change with it.
[[nodiscard]] double layersAt(const CliffNoises& n, double x, double z);

// The slope of the scree at rest (33 degrees), meters per meter.
inline constexpr double kScreeSlope = 0.65;

// The scree at the canonical (x, z) of a talus, foot meters out from the foot of a cliff cliff meters
// high, under an outline jutting out rough meters (a re-entrant: negative): the meters its blocks
// raise the talus, and how much of a cone of scree lies there, in [0, 1] (its surface falls
// kScreeSlope from the foot of the cliff: the talus rises to it where it lies lower).
struct Scree {
    double blocks = 0.0;
    double cone = 0.0;
};
[[nodiscard]] Scree screeAt(const CliffNoises& n, double x, double z, double foot, double cliff,
                            double rough);

// How deep the vertical joints of the faces open there, in [0, 2] of their depth (0: closed).
[[nodiscard]] double jointsAt(const CliffNoises& n, double x, double z);

} // namespace terrain::tableland_relief

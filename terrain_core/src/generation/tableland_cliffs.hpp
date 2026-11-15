#pragma once

// The cliffs of the plateaus (agent 46, tableland_relief.hpp), internal to terrain_core: what makes
// a caprock a wall of rock and not a cookie cut (Merrick Butte, not Devils Tower):
//   - its outline rough from 20 to 200 m (alcoves, buttresses, re-entrants), added to the jag of
//     the rims (roughAt);
//   - its batter, 80 to 88 degrees by a noise (steepAt): the slope of the ground its hard layers
//     are laid over, so that their faces, vertical each, step back with the height;
//   - 2 to 4 hard layers in its height (layersAt), their count drifting from butte to butte: walls
//     near vertical between ledges of 10 to 30 m at their bounds (ledgesAt), as many as its batter
//     holds, at heights of their own; each layer its own rough outline (its shift along the outline,
//     a tilt of 10 degrees at most): the flutes of a layer end at its ledges (cliffStoreysAt);
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

#include <array>
#include <cstdint>

namespace terrain::tableland_relief {

struct CliffNoises {
    noise::FbmOctaves rough;  // the outline, 160 to 40 m
    noise::FbmOctaves grit;   // and 40 to 20 m
    noise::FbmOctaves steep;  // the batter
    noise::FbmOctaves layers; // how many hard layers a caprock holds
    noise::FbmOctaves blocks; // where the blocks of scree lie
    noise::FbmOctaves joints; // where the joints of the faces open
    noise::FbmOctaves tilt;   // how the flutes of the layers lean
    noise::FbmOctaves ledges; // which bounds of the layers hold a ledge
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

// The layer of the stack of the forced hard layers whose lower bound tops a caprock
// (tableland_strata.cpp: unitPass, unitBench).
inline constexpr std::int64_t kTopLayerOfUnit = 8;

// The ledges of a caprock cliff meters high whose batter is steep (steepAt) at the canonical (x, z),
// its hard layers drawn from the stack units: at some of the bounds of its layers (drawn: meters
// under its top, from the top down, and the layer of the stack under each), each as wide as it is
// drawn (10 to 30 m) and as many (3 at most) as the run of its batter holds besides the walls between
// them; the slope of those walls (steeper as the ledges take more of the run).
inline constexpr std::size_t kLedgesMost = 3;
struct Ledges {
    std::size_t count = 0;
    std::array<double, kLedgesMost> drop{};
    std::array<double, kLedgesMost> width{};
    std::array<std::int64_t, kLedgesMost> layer{};
    // How far each bound stands as a step of its own (0 near the foot: the layer under it keeps the
    // rough outline of the layer over it there, its ledge none).
    std::array<double, kLedgesMost> held{};
    double wall = 1.0;
};
[[nodiscard]] Ledges ledgesAt(const CliffNoises& n, std::uint64_t units, double x, double z,
                              double cliff, double steep);

// The storeys of a cliff of ledges ledges, cliff meters high, then its talus of mean slope talus
// down to height meters under its top, at the canonical (x, z) whose distance to the outline of the
// cliff without its rough is outline (positive within, its unit gradient): each layer of the cliff
// with its own rough outline (roughAt shifted along the outline by its own amount and leaning by
// its tilt), blended across each ledge.
[[nodiscard]] Storeys cliffStoreysAt(const CliffNoises& n, std::uint64_t units, double x, double z,
                                     const Field& outline, double height, double cliff,
                                     double talus, const Ledges& ledges);

// The slope of the scree at rest (33 degrees), meters per meter.
inline constexpr double kScreeSlope = 0.65;

// The scree at the canonical (x, z) of a talus, foot meters out from the foot of a cliff cliff meters
// high (its foot without the rough outline: negative within), under an outline jutting out rough
// meters (a re-entrant: negative): the meters its blocks raise the talus (coming in over its first
// kBlockIn meters), and how much of a cone of scree lies there, in [0, 1] (its surface falls
// kScreeSlope from the foot of the cliff, and rises into the re-entrants: the talus rises to it
// where it lies lower).
struct Scree {
    double blocks = 0.0;
    double cone = 0.0;
};
[[nodiscard]] Scree screeAt(const CliffNoises& n, double x, double z, double foot, double cliff,
                            double rough);

// How deep the vertical joints of the faces open there, in [0, 2] of their depth (0: closed).
[[nodiscard]] double jointsAt(const CliffNoises& n, double x, double z);

} // namespace terrain::tableland_relief

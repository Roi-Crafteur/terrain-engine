#pragma once

// The great forms of the mountains of the planetary geography (phase 11R bis), internal to
// terrain_core: valleys in U and dominant summits, laid on the ridged massifs of the Heightfield.
//
// Valleys: main valleys along the zero lines of a strongly warped noise (sinuous, unevenly
// spaced), tributaries along those of two shorter noises, each kept on one side of the main
// valley and faded out away from it: they join it in T, branch and end in passes or cirques.
// Each has a flat floor, to which the massif comes down steeply over the width of its walls,
// its crests and hollows kept (a U, with shoulders); the floors of the main valleys climb to
// passes, those of some tributaries hang above the main valley.
//
// Summits: at most one per cell of about 16 km of the warped space of the massif, off the center
// of its cell, moved onto the nearest crest of the first octave of the massif. The massif rises
// around it, its crests much more than its hollows (the caller multiplies it): the summit is that
// crest lifted, its ridges, faces and gullies those of the massif, steeper: 2 to 5 ridges as they
// come, asymmetric faces, no geometric figure.
//
// Pure arithmetic of doubles, noises and hashes: the same bits on every platform, periodic on
// the world.

#include "terrain/generation/noise_octaves.hpp"
#include "terrain/world_size.hpp"

#include <array>
#include <cstdint>

namespace terrain::mountain_forms {

struct Noises {
    noise::FbmOctaves warpX; // the warp of the valleys
    noise::FbmOctaves warpZ;
    noise::FbmOctaves main;  // the main valleys, along its zero lines
    noise::FbmOctaves sideA; // the tributaries on the positive side of the main noise
    noise::FbmOctaves sideB; // and on its negative side
    noise::FbmOctaves passes; // the floors of the main valleys climbing to passes
    noise::FbmOctaves widths; // the widths of their floors
    noise::FbmOctaves hanging; // the tributaries hanging above the main valley
    noise::FbmOctaves presence; // where the main valleys are
    noise::FbmOctaves crests;   // the first octave of the massif: its zero lines are its crests
    std::uint64_t summits = 0;
    int cellsX = 1; // cells of the summits along each side of the world
    int cellsZ = 1;
    WorldSize world;
    bool twinWarps = false;
};

// crests: the first octave of the ridged noise of the massif alone (the same seed, lattice and
// frequency).
[[nodiscard]] Noises makeNoises(std::uint64_t seed, const WorldSize& world,
                                noise::FbmOctaves crests);

// The memos of the noises during a batch call of the Heightfield, and the summits of the 3 x 3
// cells around the last cell met.
struct Summit {
    double x = 0.0; // meters from the center of the cell met
    double z = 0.0;
    double prominence = 0.0; // 0 without a summit
    double radius = 1.0;     // of its rise, meters
};
struct Memo {
    noise::FbmOctaves::Memo warpX;
    noise::FbmOctaves::Memo warpZ;
    noise::FbmOctaves::Memo main;
    noise::FbmOctaves::Memo sideA;
    noise::FbmOctaves::Memo sideB;
    noise::FbmOctaves::Memo passes;
    noise::FbmOctaves::Memo widths;
    noise::FbmOctaves::Memo hanging;
    noise::FbmOctaves::Memo presence;
    bool valid = false;
    int cellI = 0;
    int cellJ = 0;
    std::array<Summit, 9> summits{};
};

// A form at a point: its height above the regional elevation (meters) and its gradient
// (meters per meter, up).
struct Form {
    double height = 0.0;
    double gradX = 0.0;
    double gradZ = 0.0;
};

// The massif at (x, z) (its form, in mountains up to height meters high) brought down to the
// valleys, and how much the point lies on their floors (1 on a flat floor, 0 from the foot of the
// walls on).
struct Carve {
    Form form;
    double floor = 0.0;
};
[[nodiscard]] Carve valleys(const Noises& noises, double x, double z, double height,
                            const Form& massif, Memo* memo);

// The summit of cell (i, j) of the summits (wrapped), at (x, z) in meters of the warped space
// of the massif; a prominence of 0 without one.
[[nodiscard]] Summit summitOfCell(const Noises& noises, int i, int j);

// The rise of the summits around (x, z) of the warped space of the massif (the gradients along
// it), in mountains up to height meters high, core in [0, 1]
// where they stand (the cores of the chains): the factor by which the massif rises (its crests
// far more than its hollows, then lower around), and the height added under the summits (their
// base), with their gradients. The massif becomes massif * factor + base.
struct Rise {
    Form factor{1.0, 0.0, 0.0};
    Form base;
};
// ground: the regional elevation under the massif; the summits rise at most to kCeiling.
[[nodiscard]] Rise summits(const Noises& noises, double x, double z, double ground,
                           double height, double core, Memo* memo);

} // namespace terrain::mountain_forms

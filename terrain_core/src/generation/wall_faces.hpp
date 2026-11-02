#pragma once

// The faces of the walls of the mountains in three dimensions (phase 12A), internal to
// terrain_core. Where a pass of the walls (mountain_walls.hpp: the bands of the strata and their
// rock steps, the joints of the crystalline rock) climbs the riser of a hard layer, the columns of its run
// see a face (rock_faces.hpp) standing at its middle: vertical, its top jutting out over the air
// (a hard bank) and its foot receding (the rock under it worn away) by draws of its layer and a
// noise along the wall, never a regular row of overhangs. Where the wall is faint (its mask times
// the hardness of its layer below kFaintLow) the relief keeps its slope; up to kFaintHigh the face
// comes in. A column lies within a run where the ground of its pass lies within the riser, which
// needs no slope; the Heightfield then places the face along the slope of the ground of the walls
// it measures there (the gradient of the great forms only orients the walls at large), and anchors
// it on the relief at the edges of its run. Pure arithmetic of doubles, noises and hashes: the
// same bits on every platform.

#include "mountain_walls.hpp"
#include "terrain/generation/rock_faces.hpp"

#include "terrain/generation/noise_octaves.hpp"
#include "terrain/world_size.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace terrain::wall_faces {

// The noises of the faces: their overhangs come and go along them, and the beds of their rock
// wander up and down (shift); the vertical joints of their rock, the edges of cellsX by cellsZ
// cells over the world drawn by cellSeed, open by the noise joints.
struct Grain {
    noise::FbmOctaves overhangs;
    noise::FbmOctaves shift;
    noise::FbmOctaves joints;
    std::uint64_t cellSeed = 0;
    std::int64_t cellsX = 1;
    std::int64_t cellsZ = 1;
    WorldSize world;
};
[[nodiscard]] Grain makeGrain(std::uint64_t seed, const WorldSize& world);

// The vertical joint of the rock at the canonical column (x, z): how far it recedes a face there
// (0 away from one) and how wide it is. The joints are the edges of jittered cells about 17 m
// wide (not on the lattice of the tiles), open over about two thirds of their length. Each draws
// its own groove: most open, 4 to 8 m wide and 0.12 to 0.25 of that deep (its far side stays lit
// under a grazing light), about one in five tight and dark, 3 to 4.5 m wide and 0.3 to 0.4 of that
// deep; their depth and width wander along them by noises: never a regular row of grooves.
struct Joint {
    double depth = 0.0;
    double width = 0.0;
    double floor = 0.0; // the depth of its floor (the most it recedes the face)
    double seed = 0.0;  // its draw, in [0, 1): where along the height it opens (RockFace)
    // How steep its walls are at their steepest: their depth over the width of the groove, times
    // this.
    double wall = 3.0;
    // How fast the distance to it grows along x and z (1 at most, across it), and how far it runs
    // alone there: 1 away from the corners of its cells, 0 where another joint meets it (its
    // direction gives way to the other's).
    double acrossX = 1.0;
    double acrossZ = 0.0;
    double alone = 1.0;
};
[[nodiscard]] Joint jointAt(const Grain& grain, double x, double z);

// A face of the walls found at a column, before the Heightfield places it.
struct PendingFace {
    // Its heights relative to the top of the walls at the column (0 there), its weight and its
    // overhangs; its distance, run and direction unset.
    RockFace face;
    double level = 0.0; // the ground of its pass at the middle of the riser, over that at the column
    double rise = 0.0;  // the ground of its pass the riser climbs, meters
    double rate = 1.0;  // how fast the ground of its pass rises with the ground of the walls
    double liftX = 0.0; // how fast the ground of its pass is lifted along x and z (the joints)
    double liftZ = 0.0;
    // The floor of its joint (by its weight, as its depth), the gradient of the distance to it, how
    // far it runs alone and how steep its walls are (Joint).
    double jointFloor = 0.0;
    double jointX = 1.0;
    double jointZ = 0.0;
    double jointAlone = 1.0;
    double jointWall = 3.0;
};

// What the walls report at a point of the Heightfield.
struct Site {
    bool faces = false;  // the faces are wanted (otherwise the ground alone)
    bool walls = false;  // the walls stand at the point
    double ground = 0.0; // the ground they lay their layers on there
    double gradX = 0.0;  // the gradient of the great forms there
    double gradZ = 0.0;
    std::array<PendingFace, kMaxRockFaces> pending{};
    std::size_t count = 0;
};

// Adds to site the faces of the walls at the canonical column (x, z), of masks the masks of the
// walls there and column what they depend on (mountain_walls::Column).
void addFaces(const mountain_walls::Noises& noises, const Grain& grain,
              const mountain_walls::Masks& masks, const mountain_walls::Column& column, double x,
              double z, mountain_walls::Memo* memo, Site& site);

// The face of pending over a ground of the walls of gradient (gradX, gradZ) at the column: its
// distance and run in meters, the direction of the air, its overhangs and its grain within its
// run. Its heights stay relative, to anchor (anchorFace).
[[nodiscard]] RockFace placeFace(const PendingFace& pending, double gradX, double gradZ);

} // namespace terrain::wall_faces

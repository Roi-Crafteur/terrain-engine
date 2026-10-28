#pragma once

// The faces of the walls of the mountains in three dimensions (phase 12A), internal to
// terrain_core. Where a great pass of the walls (mountain_walls.hpp: the bands of the strata, the
// coarse joints of the crystalline rock) climbs the riser of a hard layer, the columns of its run
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

#include <array>
#include <cstddef>

namespace terrain::wall_faces {

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
};

// What the walls report at a point of the Heightfield.
struct Site {
    bool faces = false;  // the faces are wanted (otherwise the ground alone)
    bool walls = false;  // the walls stand at the point
    double ground = 0.0; // the ground they lay their layers on there
    double gradX = 0.0;  // the gradient of the great forms there
    double gradZ = 0.0;
    std::array<PendingFace, 2> pending{};
    std::size_t count = 0;
};

// Adds to site the faces of the walls at the canonical column (x, z), of masks the masks of the
// walls there and column what they depend on (mountain_walls::Column).
void addFaces(const mountain_walls::Noises& noises, const mountain_walls::Masks& masks,
              const mountain_walls::Column& column, double x, double z,
              mountain_walls::Memo* memo, Site& site);

// The face of pending over a ground of the walls of gradient (gradX, gradZ) at the column: its
// distance and run in meters, the direction of the air, its overhangs within its run. Its heights
// stay relative, to anchor (anchorFace).
[[nodiscard]] RockFace placeFace(const PendingFace& pending, double gradX, double gradZ);

} // namespace terrain::wall_faces

#pragma once

// The faces of the walls of the mountains in three dimensions (phase 12A), internal to
// terrain_core. Where a great pass of the walls (mountain_walls.hpp: the bands of the strata, the
// coarse joints of the crystalline rock) climbs the riser of a hard layer, the columns of its run
// see a face (rock_faces.hpp) standing at its middle: vertical, its top jutting out over the air
// (a hard bank) and its foot receding (the rock under it worn away) by draws of its layer and a
// noise along the wall, never a regular row of overhangs. The run is measured on the smooth ground
// the pass lays its layers on, along the slope of that ground. Where the wall is faint (its mask
// times the hardness of its layer below kFaintLow) the relief keeps its slope; up to kFaintHigh the
// face comes in. The heights of the faces are those of the pass, relative to the column: the
// Heightfield anchors them on the relief at the edges of their run. Pure arithmetic of doubles,
// noises and hashes: the same bits on every platform.

#include "mountain_walls.hpp"
#include "terrain/generation/rock_faces.hpp"

namespace terrain::wall_faces {

// Adds to rock the faces of the walls at the canonical column (x, z), of masks the masks of the
// walls there and column what they depend on (mountain_walls::Column). The heights of the faces are
// relative to the top of the walls at the column (0 there), to anchor (anchorFace).
void addFaces(const mountain_walls::Noises& noises, const mountain_walls::Masks& masks,
              const mountain_walls::Column& column, double x, double z,
              mountain_walls::Memo* memo, RockColumn& rock);

} // namespace terrain::wall_faces

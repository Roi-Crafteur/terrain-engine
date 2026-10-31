#pragma once

// The eyes on the steps of a stretch of falls (terrain_export_rock --steps): for each step of 10 m
// or more, its basin, its lip and its foot, each with a heading and the height of the eye, as --at
// sees them.

#include "rock_places.hpp"

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include <string>

namespace rock_tool {

// The steps of the stretch nearest to (x, z) (as addFall) dropping least meters or more, down the
// course: three eyes 1.8 m above the water of each, in its basin (halfway from its foot to the next
// lip, looking up at it), on its lip (looking down the course) and at its foot (looking up its
// face). Adds them to out as step<k>_basin, step<k>_lip and step<k>_foot and returns their list
// (field: the world with its water, to tell the eyes over it), a --at line for each.
std::string addSteps(const terrain::Heightfield& field, const terrain::Hydrology& hydrology,
                     double x, double z, double least, Places& out);

} // namespace rock_tool

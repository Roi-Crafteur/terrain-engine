#pragma once

// The falls of a course (phase 11B), internal to terrain_core: where the water of a river comes
// down a steep slope, it falls over it in steps (river_course.cpp).

#include "river_course.hpp"

#include <vector>

namespace terrain::water {

// Where the water of points (measured, their levels set) comes down a slope steeper than
// kStepSlope over more than kStepDrop meters, it falls in steps instead of running down it:
// drops down a steep face, each followed by a pool cut into the slope, flat, up to the lip of
// the next drop, where the water meets the slope again. The first drop is at the break of the
// slope. The lips are kStepRun meters apart along the slope (0.35 to 2.35 of it, drawn from where
// they are: mostly small drops, a few tall ones), the drops as high as the slope comes down over
// that run; a slope steeper than kSheer falls at once, kSheerMax meters at most per drop. The faces
// of the drops come down at 76 to 83 degrees (as the walls of the mountains: never quite
// vertical). The water keeps the level of the ends of each stretch and stands nowhere above its
// level on the slope (never above the relief).
// Marks the points of the stretches (CoursePoint::fall).
void stepFalls(std::vector<CoursePoint>& points);

} // namespace terrain::water

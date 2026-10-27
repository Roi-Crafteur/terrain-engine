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
// slope. The lips are 0.25 to 3 times kStepRun meters apart along the slope (drawn from where
// they are: mostly small drops, a few tall ones), the drops 0.6 to 1.4 times as high as the slope
// comes down over that run, a quarter of them in two with a ledge between; a slope steeper than
// kSheer falls at once, kSheerMax meters at most per drop. The faces of the drops come down at 70
// to 88 degrees. The water keeps the level of the ends of each stretch and stands nowhere above
// its level on the slope (never above the relief).
// Marks the points of the stretches (CoursePoint::fall) and the walls of their slot
// (CoursePoint::slot, slotBase of their mean drop), and raises the level their valley is carved
// from (CoursePoint::valley) by a half of their mean drop (2 to 20 meters, faded in and out at
// their ends): their slot stays cut into the floor at the lips too.
void stepFalls(std::vector<CoursePoint>& points);

} // namespace terrain::water

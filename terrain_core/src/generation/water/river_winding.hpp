#pragma once

// The winding of the slots of the falls (phase 12A), internal to terrain_core: the runs of a
// course where its water falls in steps wind, as their slots do, once their steps are set
// (river_course.cpp).

#include "river_course.hpp"

#include <vector>

namespace terrain::water {

// The axis of the run of points from to to meters along the course (straight lines every few
// meters, refined where the water falls, its steps set), half the width of its channel: sampled
// finer where it bends (every 4 m at most), all of it interpolated, it winds as its slot does (a
// noise without regular bends, waves of 30 to 100 meters for a slot 28 meters wide, longer for
// wider ones), as far aside as about the half width of its slot, its bends no sharper than its
// walls allow, kept on its course at the faces of its falls (the face in line with its pools),
// and back onto the axis smoothly at both ends (the course before and after it unmoved). Its
// levels stay as they are, along it; the caller measures the course anew.
void windFalls(std::vector<CoursePoint>& points, double from, double to, double half);

} // namespace terrain::water

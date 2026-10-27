#pragma once

// Where a tributary meets the river it joins (phase 11B), internal to terrain_core: the end of its
// course (river_course.cpp).

#include "river_course.hpp"

#include <vector>

namespace terrain::water {

// The course of a tributary (points, measured, of input in) ends where its channel first comes
// near the channel of a river it joins (in.joins), or turns down to that channel where it first
// comes down into the valley that river carved into the relief. Returns the level of its end: the
// water of that river there, or in.b.level when it joins none of them before its own end.
[[nodiscard]] float joinEarlier(const Heightfield& relief, const ReachInput& in,
                                std::vector<CoursePoint>& points);

} // namespace terrain::water

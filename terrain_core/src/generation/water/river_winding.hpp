#pragma once

// The winding of the slots of the falls (phase 12A), internal to terrain_core. A corridor of falls
// (a stretch where the water falls in steps, and the course for half a wave before and after it)
// keeps the axis of its course: the levels of its water and of its valley lie along that axis.
// Its slot, and its valley across with it, are moved aside of the axis by a winding (two noises
// without regular bends) and stretched across where the slot runs at an angle to it, so that they
// keep their width: the valley is drawn in the frame of the slot (slotFrame), its levels set by
// where the point lies along the axis alone. However steep the corridor and however tight its
// bends, its floor and its banks never step. The frame reads only the course (its points, levels,
// shapes and slots) and the relief, never how the valley is drawn: any model of the valleys draws
// a corridor in it. Pure arithmetic of doubles and hashes: the same bits on every platform.

#include "river_course.hpp"
#include "terrain/det_math.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace terrain::water {

// The windings of the corridors of course c (its levels, steps, shapes and slots set; runs: from
// the first to the last point of each stretch where its water falls, meters along it), its relief
// relief and the surface its rivers flow on: each slot winds aside by up to kWindLeast to
// kWindLeast + kWindSpread times the width of its channel (river_winding.cpp), less toward a side
// where its banks would come down under its water, over waves long enough for its bends and its
// angle to the axis (kRateMost); back onto the axis at both ends of the corridor. Sets the shift
// and the wind of the points (CoursePoint), and the line of its water (Course::line).
void windCorridors(Course& c, const Heightfield& relief, const Surface& surface,
                   const std::vector<std::array<double, 2>>& runs);

// Where the middle of the slot of course c lies at s meters along it: offset meters left of the
// axis, coming aside by rate meters per meter along it; 0 outside its corridors.
struct SlotShift {
    double offset = 0.0;
    double rate = 0.0;
    double inner = 0.0; // the half width that winds with it (SlotWinding::inner)
};
[[nodiscard]] SlotShift shiftAt(const Course& c, double s);

// The frame of the slot at a point side meters left of the axis of course c, s meters along it:
// meters across the slot (frameSide), the slot stretched by stretch there (the length of the
// slot per meter along the axis, its rate capped at kRateCap).
struct SlotFrame {
    double side = 0.0;
    double stretch = 1.0;
};
[[nodiscard]] SlotFrame slotFrame(const Course& c, double s, double side);
inline constexpr double kRateCap = 1.0;
inline constexpr double kStretchMost = 1.4142135623730951; // sqrt(1 + kRateCap^2)

// Meters across a slot offset meters left of the axis and stretched by stretch, at a point side
// meters left of the axis, inner the half width that winds with it (SlotWinding::inner): within
// it, its side from the middle of the slot divided by the stretch; then back to its side from the
// axis (the walls of its valley on the straight axis) over a band of kFadeBand meters more than
// the offset (kFadeShare times it at least: it never folds), smoothly; its side from the axis
// beyond frameOuter.
[[nodiscard]] double frameSide(double offset, double stretch, double inner, double side);
[[nodiscard]] double frameOuter(double offset, double stretch, double inner);
inline constexpr double kFadeBand = 40.0;
inline constexpr double kFadeShare = 1.6;

// Sets the farthest each winding of course c goes aside (SlotWinding::most) and what bounds its
// windings (Course::windTable; river_frame.cpp).
void boundWindings(Course& c);

// What bounds the slot of course c from from to to meters along it (the bounds of its windings):
// it lies from lo to hi meters left of the axis, its frame stretched by stretch at most; 0 and 1
// off its corridors.
struct WindBounds {
    double lo = 0.0;
    double hi = 0.0;
    double stretch = 1.0;
    double outer = 0.0; // the frame of the slot reaches no farther from the axis (frameOuter)
};
[[nodiscard]] WindBounds windBounds(const Course& c, double from, double to);

// The bounds of the windings are kept every kBoundBin meters (Course::windTable).
inline constexpr double kBoundBin = 8.0;

// The same at a point s meters along course c.
[[nodiscard]] inline WindBounds windAt(const Course& c, double s) {
    const WindTable& t = c.windTable;
    const double u = (s - t.from) / kBoundBin;
    if (t.levels.empty() || !(u >= 0.0) || !(u < static_cast<double>(t.levels[0].size() / 4))) {
        return {}; // off the corridors
    }
    const float* b = t.levels[0].data() + 4 * static_cast<std::size_t>(u);
    return {b[0], b[1], b[2], b[3]}; // 0 between lo and hi (measured from 0)
}

// How far points beyond meters beyond the ends of a segment at least, sideLo to sideHi meters left
// of its line, lie from it in the frame of a slot w bounds (slotFrame): their side from the axis
// beyond its outer reach; within it, never nearer than the least of their side from the middle of
// the slot and from the axis, divided by its stretch (frameSide); less kWindMargin meters (far
// more than the rounding).
inline constexpr double kWindMargin = 1e-3;
[[nodiscard]] inline double windDistance(const WindBounds& w, double beyond, double sideLo,
                                         double sideHi) {
    const double near = sideLo <= 0.0 && sideHi >= 0.0 ? 0.0 : std::min(std::abs(sideLo),
                                                                        std::abs(sideHi));
    const double gap = std::max({0.0, w.lo - sideHi, sideLo - w.hi});
    const double across = near >= w.outer ? near : std::min(gap, near) / w.stretch;
    return std::max(0.0, dmath::hypot(beyond, across) - kWindMargin);
}

// The segments within kWindWindow meters of a corridor draw their valley in its frame
// (CoursePoint::windMost).
inline constexpr double kWindWindow = 16.0;

} // namespace terrain::water

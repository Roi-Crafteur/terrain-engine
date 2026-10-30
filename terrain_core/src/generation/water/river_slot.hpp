#pragma once

// The slot of a fall in steps (phase 11B), internal to terrain_core: the ravine its water falls
// down in, cut into the floor of its valley (river_profile.cpp), and the blocks of rock in its
// pools. The walls of the slot reach beyond its channel by a width of their own on each side,
// drawn along the course (noises of kSlotWave meters without zeros at regular intervals, their
// edges broken over kEdgeWave, with rare spurs of rock of all sizes jutting into it): never by how
// deep the steps lie below the floor, so that its rim never jumps at the foot of a step (the teeth
// of a slot as wide as its depth). Below a tall step its walls are only higher and steeper. Pure
// arithmetic of doubles and hashes: the same bits on every platform.

#include "river_course.hpp"

#include <cstdint>

namespace terrain::water {

// The walls of the slot of a stretch whose steps drop mean meters on average reach about
// slotBase(mean) meters beyond its channel (CoursePoint::slot).
[[nodiscard]] float slotBase(double mean);

// A noise of s within [-1, 1] (about a half at most) over waves of about wave meters, for stream
// of seed: a gradient noise plus a value noise on its lattice, shifted by a draw, so that it never
// comes back to 0 at regular intervals.
[[nodiscard]] double slotNoise(std::uint64_t seed, std::uint64_t stream, double s, double wave);

// The slot at s meters along a course of slots seed, half its channel there: its channel, on the
// axis of the course, narrowed (never wider than half).
[[nodiscard]] double slotHalf(std::uint64_t seed, double s, double half);

// How far the walls of the slot there reach beyond its channel, base its walls, on the side of a
// point side meters to the left of the slot (right: negative), the ravine wandering about the
// channel; less by bowl meters where a bowl widens the channel (bowlAt), as far as they reach.
[[nodiscard]] double slotRun(std::uint64_t seed, double s, double base, double half, double side,
                             double bowl = 0.0);

// The farthest the walls of a slot of base its walls and half its channel reach beyond it
// (slotRun), at any s and on either side.
[[nodiscard]] double slotReach(double base, double half);

// The walls of a slot out meters beyond its channel, run its walls there, rise the floor above the
// water: how high above the water. A ledge rising gently along the water (kLedgeRise meters per
// meter, never above the floor), then a cliff up to the floor over the last kCliffShare of run:
// whatever the depth of the steps, the rim stands where the cliff meets the floor.
[[nodiscard]] double slotWall(double out, double run, double rise);

// The pools of the stretches in steps of c (its levels set, Course::pools): below each drop of
// kLipLeast meters at least (river_lips.hpp), up to the next lip or the end of the stretch.
void placePools(Course& c);

// The bowl of a pool at the foot of its drop, s meters along course c: its bed deeper by deeper
// meters in the middle of its channel (kBowlShare of the drop, kBowlLeast to kBowlMost meters),
// its channel wider by wider of itself (kBowlWiden at most), the walls of its slot giving way to
// it. Full at the foot of the face (from nothing at its lip) and for a while below it, then
// shallower down to nothing within kBowlPool of its pool; nothing elsewhere.
struct Bowl {
    double deeper = 0.0;
    double wider = 0.0;
};
[[nodiscard]] Bowl bowlAt(const Course& c, double s);
// The same for the valley of segment i of the course (from its point i to the next, found from
// there), s meters along it read no farther than kBowlBeyond meters beyond its ends: where
// CoursePoint::bowlDeep and bowlWide bound it.
[[nodiscard]] Bowl bowlAt(const Course& c, double s, std::size_t i);
inline constexpr double kBowlMost = 8.0;
inline constexpr double kBowlWiden = 0.3;
inline constexpr double kBowlBeyond = 16.0;

// The blocks of rock of the stretches in steps of c (its levels, shapes and slots set): heaps at
// the foot of their drops, a block per few meters of the drop, of 1.5 to 7 meters (mostly small),
// nearly a half of them 1 to 4 meters out of their water, the others sunk, never within
// kBlockLip of the level of the step above them; in the slot where it winds (its windings set,
// river_winding.hpp), on the bed of its bowls; and their box.
void placeBlocks(Course& c);

// The surface of the blocks of course c at (x, z), in its images: -infinity off them.
[[nodiscard]] double blocksAt(const Course& c, double x, double z);

// The blocks of course c stand over the lowest valley v of its segments at (x, z): over every
// segment alike, so over the lowest as well (river_profile.hpp). The water stands only around
// them.
struct Valley;
void standBlocks(const Course& c, double x, double z, Valley& v);

} // namespace terrain::water

#pragma once

// The slot of a fall in steps (phase 11B), internal to terrain_core: the ravine its water falls
// down in, cut into the floor of its valley (river_profile.cpp), and the blocks of rock in its
// pools. The walls of the slot reach beyond its channel by a width of their own on each side,
// drawn along the course (a gradient noise of kSlotWave meters, its edges broken over kEdgeWave,
// with spurs of rock jutting into it): never by how deep the steps lie below the floor, so that its
// rim never jumps at the foot of a step (the teeth of a slot as wide as its depth). Below a tall
// step its walls are only higher and steeper. Pure arithmetic of doubles and hashes: the same bits
// on every platform.

#include "river_course.hpp"

#include <cstdint>

namespace terrain::water {

// The walls of the slot of a stretch whose steps drop mean meters on average reach about
// slotBase(mean) meters beyond its channel (CoursePoint::slot).
[[nodiscard]] float slotBase(double mean);

// The slot at s meters along a course of slots seed, half its channel there: its channel, on the
// axis of the course, narrowed (never wider than half).
[[nodiscard]] double slotHalf(std::uint64_t seed, double s, double half);

// How far the walls of the slot there reach beyond its channel, base its walls, on the side of a
// point side meters to the left of the axis of the course (right: negative), the ravine wandering
// about the channel.
[[nodiscard]] double slotRun(std::uint64_t seed, double s, double base, double half, double side);

// The farthest the walls of a slot of base its walls and half its channel reach beyond it
// (slotRun), at any s and on either side.
[[nodiscard]] double slotReach(double base, double half);

// The walls of a slot out meters beyond its channel, run its walls there, rise the floor above the
// water: how high above the water. A ledge rising gently along the water (kLedgeRise meters per
// meter, never above the floor), then a cliff up to the floor over the last kCliffShare of run:
// whatever the depth of the steps, the rim stands where the cliff meets the floor.
[[nodiscard]] double slotWall(double out, double run, double rise);

// The blocks of rock in the pools of the stretches in steps of c (its levels, shapes and slots
// set): domes kBlockLeast to kBlockMost meters wide, their tops from kBlockSunk below its water to
// kBlockOut above it, never within kBlockLip of the level of the step above them; and their box.
void placeBlocks(Course& c);

// The surface of the blocks of course c at (x, z), in its images: -infinity off them.
[[nodiscard]] double blocksAt(const Course& c, double x, double z);

} // namespace terrain::water

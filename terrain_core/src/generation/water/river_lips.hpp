#pragma once

// The lips of the falls in steps (phase 12A), internal to terrain_core. The edge a drop of a
// stretch in steps falls over is never a straight cut across its slot. Where the rock of its lip
// is harder than the rock under it (the layers of the walls of the gorges,
// Heightfield::rockHardness), the water wears the soft rock away under the lip and its middle
// retreats upstream: an amphitheater, as deep as the drop is high times the contrast of their
// hardness, more with more water. Over rock of one hardness it stays about straight, its edge only
// irregular. The valley of the course is moved downstream by as much as the lip recedes there,
// back to where it is a little below it (cutLips): the face, the pool below and the walls of the
// slot follow the edge, the water of the pool below standing in the amphitheater, the pool above
// left as it is. Never the biomes. Pure arithmetic of doubles and hashes: the same bits on every
// platform.

#include "river_profile.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace terrain::water {

// The lips of the drops of course c (its points, shapes and slot set: a face comes down more
// than kLipSlope meters per meter, kLipLeast meters at least), their edges shaped by the rock of
// relief under them and by discharge (m^3/s, the river there), and the box of what they move.
void placeLips(Course& c, const Heightfield& relief, double discharge);
inline constexpr double kLipSlope = 1.0;
inline constexpr double kLipLeast = 2.0;

// How far the edge of lip l recedes upstream, side meters left of the axis: 0 from its width on;
// recessMost at most. Never a straight cut: a bow, waves across it, the roughness of its rock and
// a turn, within the room its pools leave.
[[nodiscard]] double lipRecess(const FallLip& l, double side);
[[nodiscard]] double recessMost(const FallLip& l);

// The valley of course c near lip l at (x, z) (its segments around the lip alone).
using LipValley = std::function<Valley(double x, double z, const FallLip& l)>;

// The valley v of course c at (x, z) with the edges of its lips: where the point lies near a lip
// and within its width, the valley near the lip at the point moved downstream as far as the edge
// recedes there, where that one lies lower (its surface and its water).
// lips: those of c that may cut the point (lipsNear), or null (all of them).
void cutLips(const Course& c, double x, double z, const LipValley& near, Valley& v,
             const std::vector<std::uint32_t>* lips = nullptr);

// The lips of c that may cut a point within reach meters of (cx, cz) (their indices, in order).
[[nodiscard]] std::vector<std::uint32_t> lipsNear(const Course& c, double cx, double cz,
                                                  double reach);

} // namespace terrain::water

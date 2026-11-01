#pragma once

// The reefs of the coasts (agent 33, coast.hpp), internal to terrain_core: groups of two to four
// low rocks off some capes between coves, until the 3D rock of the faces makes stacks of them
// (commit 5). In 2D a stack cannot be both of rock (a slope of 1 at least) and credible (the slope
// law of the tiles; any ledge, saddle or flat top is soil): the rocks stay low domes.

#include "coast_inlets.hpp"
#include "terrain/world_size.hpp"

#include <functional>
#include <vector>

namespace terrain::coast_reefs {

// A rock is a dome: its flank straight, its foot rounded over kRockRim of its radius and its top
// over kRockTop (both over two cells of the LODs where it stands two cells high), its foot widened
// by lobes up to kRockLobe of its radius.
inline constexpr double kRockRim = 0.3;
inline constexpr double kRockTop = 0.3;
inline constexpr double kRockLobe = 0.4;

// The radius of a rock height meters high: its flank kRockEase of the slope law the tiles draw
// without teeth (coast_shape::faceSlopeLimit; its distance is exact, no slack); its lobes only
// make it gentler.
[[nodiscard]] double rockRadius(double height);

// The rocks of a reef off the cape at (x, z): its middle moved out at sea (kReefGap meters of sea
// at least between the reef and the shore of the geography), none where it finds no sea there or
// where inside(middle) is false; then two to four rocks around it, each out at sea, merging into
// an irregular group. Draws from d.
[[nodiscard]] std::vector<coast_inlets::Stack> reefOf(
    const coast_inlets::World& w, coast_inlets::Draws& d, double x, double z,
    const std::function<bool(double x, double z)>& inside);

// What a rock makes of the canonical point (x, z): the islet (how far into it) and the dome of the
// effect; overlapping rocks merge rounded.
void addRock(const coast_inlets::Stack& rock, double x, double z, const WorldSize& world,
             coast_inlets::Effect& e);

} // namespace terrain::coast_reefs

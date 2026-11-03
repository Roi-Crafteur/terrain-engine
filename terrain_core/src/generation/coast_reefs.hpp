#pragma once

// The reefs of the coasts (agent 33, coast.hpp), internal to terrain_core: groups of two to four
// low rocks off some capes between coves, until the 3D rock of the faces makes stacks of them
// (commit 5). In 2D a stack cannot be both of rock (a slope of 1 at least) and credible (the slope
// law of the tiles; any ledge, saddle or flat top is soil): the rocks stay low domes.

#include "coast_inlets.hpp"
#include "rock_spires.hpp"
#include "terrain/generation/noise_octaves.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace terrain::coast_reefs {

// A rock is a dome: its flank straight, its foot rounded over kRockRim of its radius and its top
// over kRockTop (both over two cells of the LODs where it stands two cells high), its foot widened
// by lobes up to kRockLobe of its radius.
inline constexpr double kRockRim = 0.3;
inline constexpr double kRockTop = 0.3;
inline constexpr double kRockLobe = 0.4;

// The shape of a rock at u, 0 at its foot to 1 at its middle, in [0, 1] of its height: its flank
// straight (steepest), its foot rounded over kRockRim of the way, its top over kRockTop (a dome),
// the slope continuous.
[[nodiscard]] double domeOf(double u);

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

// A rock kSpireLeast meters high at least is a stack: a spire of rock in three dimensions
// (rock_spires.hpp: its shape drawn from spireSeed of its middle and from its radius and height)
// over its scree apron, the relief of its rock (addRock: its outline at the sea level and its
// apron, no dome).
inline constexpr double kSpireLeast = 20.0;
[[nodiscard]] std::uint32_t spireSeed(double x, double z);
[[nodiscard]] rock_spires::Shape spireOf(const coast_inlets::Stack& rock);

// The relief of the rocks at a small scale: a noise of kRoughWavelength meters (four octaves)
// scaling their domes by about 1 +- kRough (none at their foot, at the water), as strong as the
// tiles draw it without teeth (coast_inlet_shapes_test).
[[nodiscard]] noise::FbmOctaves makeRough(std::uint64_t seed, const WorldSize& world);

// What a rock makes of the canonical point (x, z): the islet (how far into it) and the dome of the
// effect, rough; overlapping rocks merge rounded.
void addRock(const coast_inlets::Stack& rock, double x, double z, const WorldSize& world,
             const noise::FbmOctaves& rough, coast_inlets::Effect& e);

} // namespace terrain::coast_reefs

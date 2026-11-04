#pragma once

// The relief of the floors of the seas at the scale of their hills (agent 51), internal to
// terrain_core: the abyssal hills in the frame of their ridge (along its axis and across it), the
// fracture zones and transform offsets along each axis, the isolated seamounts and guyots, and
// the slow noises that vary the margins along the coasts. Deterministic, periodic on the world.

#include "terrain/generation/noise_octaves.hpp"
#include "terrain/generation/seas.hpp"
#include "terrain/world_size.hpp"

#include "sea_floor.hpp"

#include <array>
#include <cstdint>

namespace terrain::sea_hills {

inline constexpr int kHillOctaves = 4;

struct Noises {
    // The octaves of the hills, one noise each, on a plane far larger than the world (the frames
    // of the ridges, whose coordinates are not those of the world): no period within it.
    std::array<noise::FbmOctaves, kHillOctaves> hills;
    noise::FbmOctaves amplitude; // their variation over the world
    // The shelf width, the break depth, the slope gradient, the foot depth, the rise gradient,
    // the presence of a terrace and its depth on the margins, each a share in [0, 1] of its range.
    std::array<noise::FbmOctaves, 7> margins;
    // The oceanic plateaus where the first is highest (above a threshold drawn over the ocean),
    // the depth of their top from the second.
    noise::FbmOctaves plateau;
    noise::FbmOctaves plateauTop;
    noise::FbmOctaves seamountClusters; // the provinces of the seamounts
};

[[nodiscard]] Noises makeNoises(std::uint64_t seed, const WorldSize& world, const SeaParams& p);

// The hills at (u, v) of a frame (meters along and across the axis of the ridge), for squares of
// footprint meters (the octaves finer than the footprint fade out: their mean, 0), of rms about 1
// at a point.
[[nodiscard]] double hillsAt(const Noises& n, const SeaParams& p, double u, double v,
                             double footprint);
// Their variation at the canonical point (x, z) of the world: 1 +- hillsVariation.
[[nodiscard]] double hillsAmplitude(const Noises& n, const SeaParams& p, double x, double z);

// The noise of the oceanic plateaus at the canonical point (x, z) (Noises::plateau).
[[nodiscard]] double plateauNoise(const Noises& n, double x, double z);
// The depth of the top of an oceanic plateau there, meters (positive).
[[nodiscard]] double plateauTop(const Noises& n, const SeaParams& p, double x, double z);

// The margin at the canonical point (x, z), active by the share active in [0, 1].
[[nodiscard]] sea_floor::Margin marginOf(const Noises& n, const SeaParams& p, double x, double z,
                                         double active);

// The margin of a small island at the canonical point (x, z): a narrow shelf, a steep slope, no
// rise.
[[nodiscard]] sea_floor::Margin islandMarginOf(const Noises& n, const SeaParams& p, double x,
                                               double z);

// The fracture zones of ridge `ridge` at u along its axis: the offset of the axis there (blended
// across each zone) and how deep the trough of the nearest zone digs (meters, positive).
struct Fracture {
    double offset = 0.0;
    double trough = 0.0;
};
[[nodiscard]] Fracture fractureAt(std::uint64_t seed, int ridge, const SeaParams& p, double u);

// How high the isolated seamounts and guyots rise at the canonical point (x, z) of the world over
// the floor floor (meters, >= 0): none on floors shallower than about 2,000 m; dense in some
// provinces, sparse in others (Noises::seamountClusters).
[[nodiscard]] double seamountsAt(const Noises& n, std::uint64_t seed, const WorldSize& world,
                                 const SeaParams& p, double x, double z, double floor);

} // namespace terrain::sea_hills

#pragma once

// The local relief of the planetary geography (phase 11R bis), internal to terrain_core: what
// rises and falls around the regional elevation of the map at the scale of a walk, under the
// ridged mountains of the Heightfield. Rolling hills (an fbm of 4 octaves from 6 km), wide and
// shallow vallons (a smooth trough along the zero lines of a slow fbm: sinuous lines, never the
// grid of the map), buttes (flat tops with steep sides where a third fbm passes a threshold).
// The vallons and the buttes vary from region to region (a fourth fbm of 200 km): lands of
// vallons, lands of buttes, lands of plain rolling hills.
// It goes below the regional elevation as much as above it: hollows, combes and vallons are
// part of it, the water follows the relief (phase 11B). Deterministic and periodic on the
// world, like the noises.

#include "terrain/generation/noise_octaves.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>

namespace terrain::relief_local {

struct Noises {
    noise::FbmOctaves hills;
    noise::FbmOctaves vallons;
    noise::FbmOctaves buttes;
    noise::FbmOctaves regions;
};

// The memos of the noises during a batch call of the Heightfield.
struct Memos {
    noise::FbmOctaves::Memo hills;
    noise::FbmOctaves::Memo vallons;
    noise::FbmOctaves::Memo buttes;
    noise::FbmOctaves::Memo regions;
};

[[nodiscard]] Noises makeNoises(std::uint64_t seed, const WorldSize& world);

// The relief at (x, z), canonical, of this amplitude (meters): the hills within +-amplitude,
// the vallons down to kVallonDepth amplitude below them, and buttes butteHeight meters high
// (none, nor their noise, at 0). memos may be null.
inline constexpr float kVallonDepth = 0.5f;
[[nodiscard]] float rolling(const Noises& noises, double x, double z, float amplitude,
                            float butteHeight, Memos* memos);

// The relief bounded below so that it never takes room meters or more from the ground (room <= 0:
// it only rises): the part below 0 becomes -room tanh(-local / room), the same within a few
// percent while it is small next to room.
[[nodiscard]] float boundBelow(float local, float room);

} // namespace terrain::relief_local

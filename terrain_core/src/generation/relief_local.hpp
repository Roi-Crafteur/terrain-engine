#pragma once

// The local relief of the planetary geography (phase 11R bis), internal to terrain_core: what
// rises and falls around the regional elevation of the map at the scale of a walk, under the
// ridged mountains of the Heightfield. Rolling hills (an fbm of 3 or 4 octaves), wide and shallow
// vallons (a smooth trough along the zero lines of a slow fbm: sinuous lines, never the grid of
// the map), buttes (flat tops with steep sides where a third fbm passes a threshold). The
// vallons and the buttes vary from region to region (the regions of the map): lands of vallons,
// lands of buttes. The lively plains (GeographyParams::livelyPlains) have more relief at the
// scale of a walk (shorter hills in the plains than in the rough country), vallons and buttes
// everywhere (only denser in their lands) and soft terraces between the lands; without them, the
// relief of phase 11R bis step 2.
// It goes below the regional elevation as much as above it: hollows, combes and vallons are
// part of it, the water follows the relief (phase 11B). Deterministic and periodic on the
// world, like the noises.

#include "terrain/generation/noise_octaves.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>

namespace terrain::relief_local {

struct Noises {
    noise::FbmOctaves hills;
    noise::FbmOctaves broadHills; // lively plains: the hills of the rough country, broader
    noise::FbmOctaves vallons;
    noise::FbmOctaves buttes;
    bool lively = true;
};

// The memos of the noises during a batch call of the Heightfield.
struct Memos {
    noise::FbmOctaves::Memo hills;
    noise::FbmOctaves::Memo broadHills;
    noise::FbmOctaves::Memo vallons;
    noise::FbmOctaves::Memo buttes;
};

[[nodiscard]] Noises makeNoises(std::uint64_t seed, const WorldSize& world, bool lively);

// What the relief at a point is made of.
struct Shape {
    float amplitude = 0.0f; // meters: the hills within +-amplitude, the vallons below them
    float buttes = 0.0f;    // meters high (none, nor their noise, at 0)
    float region = 0.0f;    // the regions of the map: vallons above 0, buttes below
    float plain = 1.0f;     // [0, 1], the vallons and terraces fade out under the mountains
    // [0, 1], from the hills of the plains (shorter, the forms seen at the height of a walker)
    // to those of the rough country (broader, under the foothills and mountains).
    float rough = 0.0f;
    // (0, 1], the share of the amplitude the coasts leave (the factor of the land): the terraces
    // step the relief before it, along the hills and not along the shore.
    float fade = 1.0f;
};

// The relief at (x, z), canonical. memos may be null.
inline constexpr float kVallonDepth = 1.0f;
[[nodiscard]] float rolling(const Noises& noises, double x, double z, const Shape& shape,
                            Memos* memos);

// The relief bounded below so that it never takes room meters or more from the ground (room <= 0:
// it only rises): the part below 0 becomes -room tanh(-local / room), the same within a few
// percent while it is small next to room.
[[nodiscard]] float boundBelow(float local, float room);

} // namespace terrain::relief_local

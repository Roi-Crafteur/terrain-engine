#pragma once

// The ground of the geography along its coasts, internal to terrain_core: the coastal plain and the
// shelf of phase 11R (moved here from heightfield_geography.cpp, bit for bit), and the band where
// the coasts of agent 33 shape them (coast.hpp).

#include "coast_kinds.hpp"
#include "terrain/generation/geography.hpp"
#include "terrain/generation/geography_params.hpp"
#include "terrain/generation/noise_octaves.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>

namespace terrain::coast_shape {

// Whether a point of coast signal coast lies in the band of the coasts: from kCoastSeaBand of
// signal at sea to coastRamp inland, both excluded.
[[nodiscard]] bool inBand(const GeographyParams& p, double coast);

// The factor of the land at a coast signal: the mountains, the hills and the forms of the plains
// fade in from the coast over coastRamp (smoothstep), 0 at sea.
[[nodiscard]] float landFactor(const GeographyParams& p, double coast);

// The sea floor near the coast at a coast signal coast < 0: down to the shelf depth over the
// shelf width, then the slope.
[[nodiscard]] double shelf(const GeographyParams& p, double coast);

// Ground of the geography g before the local relief at the coast signal coast: the shelf and the
// ocean floor below the coast, the coastal plain then the regional elevation above it. Below the
// sea level exactly where coast is negative: both sides strictly of their sign, 0 on the coast.
[[nodiscard]] float groundBase(const GeographyParams& p, const GeographySample& g, double coast);

// The noises of the shapes of the coasts: the height of the cliffs along them, the width of the
// rock platforms at their foot and the steepness of their faces, each varying at its own scale (no
// regular pattern along a coast).
struct Noises {
    noise::FbmOctaves height;
    noise::FbmOctaves platform;
    noise::FbmOctaves face;
};

[[nodiscard]] Noises makeNoises(std::uint64_t seed, const WorldSize& world);

// What the coasts make of the ground at a point of their band (agent 33): the factor of the land
// (the mountains, the hills and the forms of the plains fade in from the shore) and the ground
// before the local relief. Each kind has its own; they are blended by their weights. The low
// coasts keep landFactor and groundBase; the rocky coasts, the cliffs, the calanques and the rias
// stand up from the shore as a plateau over a face a few meters wide, a rock platform before it;
// the mountains plunge into the sea, their foot within a few kilometers of signal of the shore.
// Out at sea, the floor drops faster at the foot of the high coasts and comes back to the shelf at
// the outer edge of the band. distance: the distance to the shore in meters (the coast signal
// over its gradient), signed as the signal.
struct Ground {
    float land = 0.0f;
    float ground = 0.0f;
};

[[nodiscard]] Ground shapeAt(const GeographyParams& p, const Noises& noises, double x, double z,
                             const GeographySample& g, double coast, double distance,
                             const coast_kinds::Weights& weights);

} // namespace terrain::coast_shape

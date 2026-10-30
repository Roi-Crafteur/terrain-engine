#pragma once

// The ground of the geography along its coasts, internal to terrain_core: the coastal plain and the
// shelf of phase 11R (moved here from heightfield_geography.cpp, bit for bit), and the band where
// the coasts of agent 33 shape them (coast.hpp).

#include "terrain/generation/geography.hpp"
#include "terrain/generation/geography_params.hpp"

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

} // namespace terrain::coast_shape

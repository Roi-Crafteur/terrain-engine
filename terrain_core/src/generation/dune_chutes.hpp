#pragma once

// The chutes of the avalanches (grain flows) down the straight slip faces of the giant waves
// (dune_chutes.cpp), internal to terrain_core: gullies one by one at random places across the
// face, each its own width, depth, start and end, so that nothing lines up in rows; and the
// festoons of those faces.

#include "dune_phase.hpp"

namespace terrain::dune_chutes {

// The meters the chutes sink the ground at (x, z), d meters downwind of the brink of a straight
// slip face run meters long (0 off it), less and less from kChuteNear to kChuteFar of the
// footprint of the sample (dune_phase::Shared::footprint: none on the far horizon, where its
// grid would draw them in rows of spots).
[[nodiscard]] double depth(const Ergs::Impl& m, const dune_phase::Shared& s, double x, double z,
                           double d, double run);

// The festoons and spurs of the slip faces of the giant waves at (x, z): meters their crest and
// face move downwind (and back) there, about 450 m apart along the crest, irregular; their slope
// along the crest at most 0.45.
[[nodiscard]] double festoons(const Ergs::Impl& m, const dune_phase::Shared& s, double x, double z);

} // namespace terrain::dune_chutes

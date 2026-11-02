#pragma once

// The waves of the ergs of the characters big waves and giant waves (agent 34, ergs.hpp:
// ErgCharacter), internal to terrain_core: a continuous sea of smooth forms, long curved sharp
// crests that join, broad smooth flanks and almost no flat floor.
//
// Across a crest, along the wind: the face downwind of it drops at the angle of repose (33
// degrees) at the brink, then eases along a parabola to the trough, where the back of the next
// wave starts with no slope: no floor between them; the back rises along a power curve to the
// sharp crest, at most 12 degrees steep (big waves) or 6 (giant waves). Their crests stand on the phase of the pivots
// (dune_phase.hpp): kPivot apart for the big waves, crests 2 to 6 km apart, 80 to 300 m high;
// kGiantPivot apart for the giant waves, crests 6.5 to 20 km apart, 4.5 % of their spacing high
// (300 to 800 m). A second system of crests, wider and turned, joins them (Y junctions) where a
// noise is high. Their height is capped by their spacing, so that the backs stay that gentle.

#include "dune_phase.hpp"

namespace terrain::dune_waves {

// The meters of sand of the waves at (x, z) (s: the noises there), sand and amplitude those of
// the erg point (dune_field): giant, of the giant waves, else of the big waves.
[[nodiscard]] double waves(const Ergs::Impl& m, const dune_phase::Shared& s, double x, double z,
                           double sand, double amplitude, bool giant);
// Their mean over their pattern (the far horizon), the same arguments.
[[nodiscard]] double meanWaves(const Ergs::Impl& m, const dune_phase::Shared& s, double sand,
                               double amplitude, bool giant);
// Their spacing over that of the realistic ridges (the filtering of the far horizon).
[[nodiscard]] double spacingScale(bool giant);
// The profile across a crest: t in [0, 1) past the crest along the wind, wavelength meters
// (local), height meters, backTan the steepest of the back (tangent).
[[nodiscard]] double profile(double t, double wavelength, double height, double backTan);

} // namespace terrain::dune_waves

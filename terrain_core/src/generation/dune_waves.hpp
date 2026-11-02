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
// noise is high, only near their own crests and lower than them. Their height is capped by their spacing, so that the backs stay that gentle.

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
// The same with its trough (dig: the share of the height it sinks below the floor, from the middle
// of the face to the middle of the next back): the height (negative in the trough), the slope of
// the form without the trough (tangent), the meters to the nearer crest.
struct Shape {
    double height = 0.0;
    double slope = 0.0; // falling downwind: the face and the near half of the trough
    double rise = 0.0;  // rising downwind: the back and the far half of the trough
    double crest = 0.0;
};
[[nodiscard]] Shape shapeOf(double t, double wavelength, double height, double backTan, double dig);
// The ground where the second system (join meters, already lowered to its share, top its profile
// over its height) rises over the first (main meters) by share: only near its crests, joined
// smoothly over soft meters (no fold where they meet; its long backs lift no trough of the first).
[[nodiscard]] double joined(double main, double join, double top, double share, double soft);

} // namespace terrain::dune_waves

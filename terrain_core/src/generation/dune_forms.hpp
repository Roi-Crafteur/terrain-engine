#pragma once

// The forms of the dunes (dune_forms.cpp), internal to terrain_core: pure functions of the
// position of a point in the frame of a pattern of dunes, meters of sand. Every face under the
// wind (slip face) stands at the angle of repose of dry sand, about 33 degrees; the backs facing
// the wind are gentle; the crests are sharp (the brink where the back meets the slip face).

#include <cstdint>
#include <functional>

namespace terrain::dune_forms {

inline constexpr double kLeeTan = 0.6494075931975106;    // tan 33 degrees: the slip faces
inline constexpr double kGentleTan = 0.4040262258351568; // tan 22 degrees: the other flank of a seif
inline constexpr double kStarTan = 0.5773502691896258;   // tan 30 degrees: the peak of a star

// A transverse ridge: t in [0, 1) the position past the last crest along the wind, in wavelengths;
// wavelength the local spacing of the crests, meters; height their height; gap the share of the
// wavelength left as bare floor between two dunes. The slip face drops at kLeeTan from the crest,
// the back rises over the rest of the wavelength (less the gap) along a Hermite curve. The height
// is cut so that the back stays below about 12 degrees.
// nominal: the spacing that caps the height (smooth, where wavelength may vary fast). dig: the
// share of the height the trough between two ridges sinks below the floor (trough), 0 for none.
[[nodiscard]] double transverse(double t, double wavelength, double height, double gap,
                                double nominal, double dig = 0.0);

// A trough dug between two dunes: depth meters at the middle of [from, to] (d along the same
// axis), none at their ends and beyond, with no slope at its ends nor at its bottom (the curve
// (1 - u^2)^2 of u in [-1, 1]).
[[nodiscard]] double trough(double d, double from, double to, double depth);
// The length of the back of such a ridge, meters (0 if none).
[[nodiscard]] double backLength(double wavelength, double height, double gap);

// A barchan of a lattice of rows along the wind and cells across it: t in [0, 1) the position
// past the start of the row along the wind, in rows, of wavelength meters; across in [0, 1) the
// position in the cell across the wind, of cell meters; height the most it may be; presence in
// [0, 1] how much of it there is (its whole form scaled down: its faces never steeper); crest in
// [0, 1) where its crest lies along the row. A crescent whose horns point downwind, apart from
// those of the next cells, on the bare floor; with the ends of the rows before and after it, or
// alone (single).
struct Barchan {
    double t = 0.0;
    double wavelength = 1.0;
    double across = 0.0;
    double cell = 1.0;
    double height = 0.0;
    double presence = 0.0;
    double crest = 0.5;
    bool single = false;
};
[[nodiscard]] double barchan(const Barchan& b);

// A longitudinal ridge (seif) at offset meters across its crest line (negative on its left), of
// a height: one flank at kLeeTan, the other at kGentleTan, swapped by slip in [0, 1] (0: the slip
// face on the left).
[[nodiscard]] double seif(double offset, double height, double slip);

// The star dunes of a lattice of cell meters at the canonical (x, z): a peak at kStarTan with 2 to
// 6 arms of sharp crests, uneven and bent, on the cells that hold one (occupancy), at most height high. periodX
// and periodZ: those of the world (whole multiples of cell). key: the hash of the pattern.
struct StarLattice {
    double cell = 2048.0;
    double periodX = 0.0;
    double periodZ = 0.0;
    double height = 0.0;
    double occupancy = 0.0;
    std::uint64_t key = 0;
    // Stars on a pedestal: how far below their pedestal they reach before they end (the lowest
    // ground around them), so that they end under it, never in a step.
    double depth = 0.0;
};
[[nodiscard]] double stars(const StarLattice& lattice, double x, double z);
// The ground under a star on a pedestal, the share of its height it takes there ([0, 1]), and
// its own height and depth (StarLattice::height and depth, at most the lattice's): read at its
// center only, so that its shape never changes with the point that looks at it.
struct Pedestal {
    double base = 0.0;
    double presence = 0.0;
    double height = 0.0;
    double depth = 0.0;
};
// Stars standing on a pedestal: pedestal(cx, cz) gives the ground under the star centered there;
// the star rises from it, and sinks lattice.depth under it as its presence falls to 0. -infinity
// where no star reaches (x, z).
[[nodiscard]] double starsOn(const StarLattice& lattice, double x, double z,
                             const std::function<Pedestal(double, double)>& pedestal);

// The Hermite curve 3t^2 - 2t^3 of t clamped to [0, 1].
[[nodiscard]] double hermite(double t);

// A uniform number in [0, 1) of a hash.
[[nodiscard]] inline double uniform(std::uint64_t h) {
    return static_cast<double>(h >> 11) * 0x1p-53;
}
// The hash of the integer cell (i, j) of a pattern.
[[nodiscard]] std::uint64_t cellHash(std::uint64_t key, std::int64_t i, std::int64_t j);

} // namespace terrain::dune_forms

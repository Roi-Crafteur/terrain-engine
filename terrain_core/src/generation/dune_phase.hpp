#pragma once

// The phase of the crests of the dunes (dune_phase.cpp), internal to terrain_core: a continuous
// field, never the position times a direction (far from the origin, a wind turning by a few
// degrees from one place to the next would tear such a pattern apart).
//
// Pivots stand on a lattice of kPivot meters, jittered; each holds the direction of the wind at
// its place, the spacing of its crests and a phase of its own, and measures the distance along
// its direction from itself (a few tens of kilometers at most). Each pivot gives a phasor
// exp(2 pi i q) of its phase q; a point blends the phasors of its 4 nearest pivots with smooth
// weights (of a lattice warped by the noises, summing to 1) and takes the argument of the sum:
// one system of crests, which the pivots bend gently, with the defects of a real field of dunes
// (a crest that ends, two that join in a Y) where two pivots disagree. The modulus of the sum over
// the weights (the coherence, [0, 1]) is low only around those defects.
//
// The phase of a pivot along a direction u: (distance along u + sinuosity) / spacing + its own
// phase + drift, the sinuosity and the drift being noises of the world. The local wavelength is
// that of the blended phase, from its differences over kStep meters.

#include "ergs_impl.hpp"

#include <array>
#include <cstdint>

namespace terrain::dune_phase {

inline constexpr double kPivot = 16384.0;
// The lattice of the giant waves (ergs of that character): crests 5 to 20 km apart need pivots
// farther apart. A whole multiple of kPivot: it divides the world as kPivot does.
inline constexpr double kGiantPivot = 4.0 * kPivot;
inline constexpr double kStep = 1.0; // meters: the differences of the noises and of the phase

// The noises the phases read, at the point and kStep meters east and south of it.
struct Shared {
    std::array<double, 3> x{};
    std::array<double, 3> z{};
    std::array<double, 3> sinuosity{};
    std::array<double, 3> drift{};
    // The side of the square the sample stands for (Ergs::dunesAt), meters, 0 for a point: the
    // chutes of the slip faces of the giant waves fade out from kChuteNear to kChuteFar of it.
    double footprint = 0.0;
};
[[nodiscard]] Shared sharedAt(const Ergs::Impl& m, double x, double z);

struct Pivot {
    double cx = 0.0; // meters, in the images of the point
    double cz = 0.0;
    double dx = 1.0; // the wind
    double dz = 0.0;
    double spacing = 1.0; // of its crests, meters
    double waves = 1.0;   // of the crests of the big waves (Kind::waves), meters
    double phase = 0.0;   // [0, 1)
    std::uint64_t key = 0;
};

// The 4 pivots around a point and their lattice cell; giant: those of the lattice of kGiantPivot
// (their own draws), else of kPivot.
struct Pivots {
    std::array<Pivot, 4> pivots{};
    std::int64_t fi = 0;
    std::int64_t fj = 0;
    bool giant = false;
    [[nodiscard]] double size() const { return giant ? kGiantPivot : kPivot; }
};
[[nodiscard]] Pivots pivotsAt(const Ergs::Impl& m, const Shared& s, bool giant = false);

// Which phase of the pivots: along their wind (ridges, rows of barchans, draas) or across it
// (seifs, cells of the barchans), with the spacing of each pivot scaled, its direction turned
// by turn (turns), its phase scrambled by salt (0: the phase of the pivot), its drift taken
// with the factor drift.
struct Kind {
    bool across = false;
    double spacing = 1.0;
    double turn = 0.0;
    std::uint64_t salt = 0;
    double drift = 1.0;
    double sinuous = 1.0; // the sinuosity of the crests, times that of the ridges
    // A fixed spacing in meters instead of that of the pivots (the small dunes), if positive.
    double fixedSpacing = 0.0;
    // The spacing of the big waves of the pivots (Pivot::waves) times spacing, instead.
    bool waves = false;
};

struct Phase {
    double fraction = 0.0;   // [0, 1): the position past the last crest, in wavelengths
    double wavelength = 1.0; // meters, local
    double coherence = 1.0;  // [0, 1]
    // meters: the local wavelength unclamped (one over the gradient of the phase), the
    // wavelength the distances to the crests may never exceed (they would steepen the faces)
    double local = 1.0;
    double nominal = 1.0;    // meters: the spacing of the pivots, weighted (smooth)
    double gx = 0.0;         // the gradient of the phase, cycles per meter (along x and z)
    double gz = 0.0;
    double cx = 0.0;         // the gradient of the coherence, per meter (unclamped)
    double cz = 0.0;
};
[[nodiscard]] Phase phaseAt(const Ergs::Impl& m, const Shared& s, const Pivots& p, const Kind& k);
// The phases of two kinds at once (the weights of the pivots shared): the same bits as phaseAt.
[[nodiscard]] std::array<Phase, 2> phasesAt(const Ergs::Impl& m, const Shared& s, const Pivots& p,
                                            const Kind& first, const Kind& second);
// The spacing of the pivots at the point, weighted as their phasors (Phase::nominal of the ridges),
// without their phases (waves: that of the big waves, Pivot::waves).
[[nodiscard]] double nominalAt(const Ergs::Impl& m, const Shared& s, const Pivots& p,
                               bool waves = false);

} // namespace terrain::dune_phase

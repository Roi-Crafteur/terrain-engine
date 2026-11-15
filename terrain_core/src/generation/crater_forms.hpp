#pragma once

// The forms of the impact craters of agent 52, internal to terrain_core (impact_craters.hpp places
// them). A simple crater (up to about 3.6 km across) is a bowl under a raised rim, its floor a
// fifth of its radius (Meteor Crater, Tenoumer); a complex one has a flat floor half its radius or
// more, terraces down its inner wall and a central peak (from 4 km across), a ring of peaks
// instead from 40 km across. Their depth follows the craters of the Earth: 0.16 of the diameter
// for the simple ones, 400 m times the diameter in kilometers to the power 0.3 for the complex
// ones; the rim stands 0.036 of the diameter over the ground around, a quarter of the depth at
// most, and decays outward as the cube of the distance (its uplift and its ejecta) to nothing at
// 2.5 radii. Never round: three harmonics bend the outline (squarish, like Meteor Crater), a
// fourth makes the crest rise and fall around it.
// Worn by its age, the humidity of its climate and its soft rock (impact_craters.cpp), the rim
// comes down, sediment fills the floor (flat, wider), the peak sinks into it, the terraces and
// the steep walls soften: a worn-out crater is a shallow round pan under a low ring of hills, its
// peak a hill in the middle (Manicouagan, Clearwater).
// Pure arithmetic of doubles: the same bits on every platform.

#include <array>

namespace terrain::lows {

inline constexpr double kCraterOuter = 2.5; // the ejecta end at this many radii

struct Crater {
    double x = 0.0; // canonical center
    double z = 0.0;
    double radius = 1.0; // of the crest of the rim (its mean), meters
    double depth = 1.0;  // fresh, from the crest of the rim down to the floor
    double rim = 0.0;    // fresh, the crest over the ground around
    double peak = 0.0;   // fresh, the central peak over the floor (0: none)
    double ring = 0.0;   // fresh, the ring of peaks over the floor (0: none)
    double floorShare = 0.2; // fresh, the radius of the floor, a share of the radius
    int terraces = 0;    // steps down the inner wall
    double wear = 0.0;   // 0 fresh, 1 worn out
    // Amplitudes and phases (turns) of the outline (orders 2, 3, 4) and of the crest (order 6).
    std::array<double, 4> harmonics{};
    std::array<double, 4> phases{};
    double reach = 1.0; // how far its ground may move from its center
};

// The fresh crater of a diameter (meters): its depth, rim, peak, ring, floor and terraces (0 to
// 4, of terraceDraw in [0, 1)); the center, the outline, the wear and the reach left as they are.
[[nodiscard]] Crater freshCrater(double diameter, double terraceDraw);

// How far the ground of a crater may move from its center: kCraterOuter of its widest radius.
[[nodiscard]] double craterReach(const Crater& c);

// The crater at the offset (dx, dz) from its center.
struct CraterShape {
    double delta = 0.0;  // meters over the ground before the impact
    double inside = 0.0; // [0, 1], how much of the relief of the plains it takes away (in it)
    double flat = 0.0;   // [0, 1], the share of the detail the sediment of its floor buries
    double onFloor = 0.0; // [0, 1], on its floor
};
[[nodiscard]] CraterShape shapeAt(const Crater& c, double dx, double dz);

// How far the floor of a crater lies under the ground around it as its wear leaves it, meters
// (below 0; its peak or its ring aside).
[[nodiscard]] double craterFloor(const Crater& c);

// The radius of the crest at an angle (turns from +x toward +z), meters.
[[nodiscard]] double crestRadius(const Crater& c, double turns);

} // namespace terrain::lows

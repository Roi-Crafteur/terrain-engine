#pragma once

// The profiles of the floor of the seas (agent 51), internal to terrain_core: pure functions of
// distances and ages, deterministic, without any noise (sea_hills.hpp adds the hills, the
// fracture zones and the seamounts). Elevations in meters (negative under the sea), lengths in
// meters, ages in millions of years.

namespace terrain::sea_floor {

// A continental margin seen across it: the shelf falls from the shore to breakDepth at
// shelfWidth, the continental slope from there at slopeGradient down to footDepth, the
// continental rise on at riseGradient, for the share riseShare (0 on an active margin: the slope
// goes on down). A
// marginal plateau may break the slope: a terrace from terraceDepth, terraceWidth wide, falling
// terraceGradient (none when its width is 0). The corners are rounded over corner meters of
// height.
struct Margin {
    double shelfWidth = 80'000.0;
    double breakDepth = 130.0;
    double slopeGradient = 0.07;
    double footDepth = 2'500.0;
    double riseGradient = 0.01;
    double riseShare = 1.0;
    double terraceDepth = 0.0;
    double terraceWidth = 0.0;
    double terraceGradient = 0.004;
    double corner = 60.0;
};

// The elevation of the margin d meters from the shore (d >= 0): 0 at the shore, ever deeper.
[[nodiscard]] double marginAt(const Margin& m, double d);

// The depth of the oceanic crust of an age (GDH1 of Stein and Stein, 1992: 2,600 m at the axis,
// 2,600 + 365 sqrt(age) up to 20 Myr, then 5,651 - 2,473 exp(-0.0278 age)), meters, positive.
[[nodiscard]] double depthOfAge(double age);

// The relief of the axis of a ridge v meters from it (warped meters across), for the share of
// the fastest spreading rate rate in [0, 1]: the rift valley of the slow ridges (valleyDepth
// deep at rate 0, a floor of valleyFloor half width, walls out to valleyWidth), the axial high
// of the fast ones (highHeight at rate 1, half width highWidth), nothing between.
struct Axis {
    double valleyDepth = 1'400.0;
    double valleyFloor = 5'000.0;
    double valleyWidth = 16'000.0;
    double highHeight = 350.0;
    double highWidth = 7'000.0;
    double slowBelow = 0.45; // the valley from rate 0 up to it
    double fastAbove = 0.55; // the high from it up to rate 1
};
[[nodiscard]] double axisRelief(const Axis& a, double v, double rate);

// max(a, b) and min(a, b) rounded over k (C-infinity; k = 0: the plain ones).
[[nodiscard]] double smoothMax(double a, double b, double k);
[[nodiscard]] double smoothMin(double a, double b, double k);

// 0 below edge0, 1 above edge1, the smooth Hermite step between (edge0 > edge1 allowed: the
// step goes down then).
[[nodiscard]] double step(double edge0, double edge1, double v);

} // namespace terrain::sea_floor

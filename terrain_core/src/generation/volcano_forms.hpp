#pragma once

// Internal to the volcanoes: the profiles of their forms and the noises of their shapes, in local
// coordinates around a center (meters, never world coordinates: no phase drifts far from the
// origin). Pure functions; deterministic.

#include <cstdint>

namespace terrain::volcano_forms {

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kTwoPi = 2.0 * kPi;

[[nodiscard]] double smoothstep(double edge0, double edge1, double x);
// The polynomial smooth maximum and minimum of width k (C1): max(a, b) far apart, at most k / 4
// above it (below the minimum) where they meet.
[[nodiscard]] double smax(double a, double b, double k);
[[nodiscard]] double smin(double a, double b, double k);
// The angle a - b brought into [-pi, pi).
[[nodiscard]] double angleBetween(double a, double b);

// The concave profile of a stratovolcano at rho = r / R: 1 at the summit, 0 at the foot and
// beyond; its slope at the summit sharpness / (1 - e^-sharpness) times the mean slope.
[[nodiscard]] double stratoProfile(double rho, double sharpness);
// The convex dome of a shield: (1 - rho^2)^1.5, flat at the summit, steepest on its mid flank.
[[nodiscard]] double shieldProfile(double rho);

// A smooth noise of the circle in [-1, 1]: cells cells around the turn (turns in [0, 1)),
// periodic, each cell its value from seed (cubic Hermite between them).
[[nodiscard]] double ringNoise(std::uint64_t seed, double turns, int cells);
// A smooth noise of the line in [-1, 1], a cell per unit of t.
[[nodiscard]] double lineNoise(std::uint64_t seed, double t);
// A smooth noise of the plane in [-1, 1] (values at the integer lattice, quintic fade), at
// (u, v) in cells. Local coordinates only: |u|, |v| below 2^31.
[[nodiscard]] double planeNoise(std::uint64_t seed, double u, double v);

// A cinder cone of height h and foot radius radius at distance r from its center: a truncated
// cone (the top radius 0.4 radius, slopes of about 31 degrees for the usual proportions) with
// rounded edges, its crater of radius crater and depth depth dug in its top.
[[nodiscard]] double cinderCone(double r, double h, double radius, double crater, double depth);
// A maar at distance r from its center: a bowl depth meters deep under the ground and radius in
// radius, steep walls and a flat floor, its ring of tuff ring meters high around it. Relative to
// the ground before it.
[[nodiscard]] double maar(double r, double depth, double radius, double ring);
// The wall of a crater of radius 1 at q = r / radius: 0 on its floor, 1 at its rim and beyond, the
// wall over the share wall of the radius (S-shaped: a talus at its foot, a rounded rim).
[[nodiscard]] double craterWall(double q, double wall);

} // namespace terrain::volcano_forms

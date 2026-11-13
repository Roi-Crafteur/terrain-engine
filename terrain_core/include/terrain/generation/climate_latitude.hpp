#pragma once

// The latitude the climate reads (task 11C, after the audit; flag sphere_latitude).
//
// The world is a torus mapped as a rectangle: z runs from the north pole (-periodZ / 2) to the south
// one (+periodZ / 2). Main reads the latitude linear in z, as a map in equal steps of latitude
// (180 |z| / periodZ degrees): the polar caps then cover as much land as the tropics, a third of the
// land frozen or tundra. With sphere, the climate reads instead the latitude of a sphere of the same
// poles whose area between two latitudes is the one of the band of the map between them:
// sin(lat) = z / zmax, zmax = periodZ / 2 (an equal-area map): the tropics wide, the caps small, as
// on the Earth. The continents do not move (only the climate reads it).
//
// Every reader of the latitude of the climate goes through these: the temperature of the planet
// level and of the tiles, the bands of the winds and their rain, the fjords of the coasts, the
// winds of the ergs, the maps of the settlement. Off, each gives the bits of main.

#include "terrain/det_math.hpp"

#include <algorithm>
#include <cmath>

namespace terrain {

// The sine of the latitude at z, canonical in [-periodZ / 2, periodZ / 2), signed (negative in the
// north): sin(pi z / periodZ) off (the formula of main), 2 z / periodZ with sphere.
[[nodiscard]] inline double climateSine(double z, double periodZ, bool sphere) {
    if (!sphere) {
        return dmath::sinTurns(z / (2.0 * periodZ));
    }
    return std::clamp(2.0 * z / periodZ, -1.0, 1.0);
}

// The latitude at z, canonical, in degrees from the equator, signed (negative in the north):
// 180 z / periodZ off (the formula of main), asin(2 z / periodZ) with sphere.
[[nodiscard]] inline double climateLatitude(double z, double periodZ, bool sphere) {
    if (!sphere) {
        return 180.0 * z / periodZ;
    }
    const double s = climateSine(z, periodZ, true);
    return dmath::atan2(s, std::sqrt(std::max(0.0, 1.0 - s * s))) * (180.0 / 3.14159265358979323846);
}

} // namespace terrain

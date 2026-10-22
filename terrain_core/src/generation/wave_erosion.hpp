#pragma once

// Erosion by waves (the sine wave erosion of the Big Globe mod, Builderb0y), internal to
// terrain_core: furrows and ravines that follow the slope and branch, on every slope and at
// every size, added to a relief from its gradient alone. Sine waves from the longest wavelength
// to the shortest (perOctave per octave), each turned by the golden angle from the one before;
// each weighted by the square of the slope across its direction, so that only the waves whose
// crests run down the slope count; its phase shifted by the sum already added (the branching),
// and its derivative added to the gradient for the next ones. The waves are those of the torus
// of the world (whole numbers of them along each side), so the erosion is periodic; the sines
// are polynomials of +, - and *: the same bits on every platform.

#include "terrain/world_size.hpp"

#include <cstdint>
#include <vector>

namespace terrain::wave_erosion {

struct Wave {
    double kx = 0.0; // turns per meter along x and z
    double kz = 0.0;
    double ux = 1.0; // unit direction of (kx, kz)
    double uz = 0.0;
    double phase = 0.0; // turns
    double share = 1.0; // wavelength over the longest wavelength
};

struct Waves {
    std::vector<Wave> waves;
};

// The waves from maxWavelength down to minWavelength, perOctave per octave. Throws
// std::invalid_argument unless 0 < minWavelength <= maxWavelength and perOctave >= 1.
[[nodiscard]] Waves makeWaves(std::uint64_t seed, const WorldSize& world, double maxWavelength,
                              double minWavelength, int perOctave);

// The erosion at (x, z) of a relief of gradient (dx, dz) (meters per meter), in units of the
// amplitude: about +-0.1 on steep slopes, 0 on the flat. The callers scale it, about half the
// longest wavelength in meters.
[[nodiscard]] double erode(const Waves& waves, double x, double z, double dx, double dz);

} // namespace terrain::wave_erosion

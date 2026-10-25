#pragma once

#include "terrain/world_size.hpp"

#include <cstdint>

namespace terrain::noise {

namespace detail {

inline constexpr std::uint64_t kGolden = 0x9E3779B97F4A7C15ULL;

// splitmix64 finalizer: every input bit affects every output bit.
constexpr std::uint64_t mix64(std::uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

} // namespace detail

// Independent seed for a layer or an octave of a generator (splitmix64 mixing). Inline: the cells
// of the relief (walls, summits, ravines, hills) derive one at every point.
[[nodiscard]] constexpr std::uint64_t deriveSeed(std::uint64_t seed, std::uint64_t stream) {
    return detail::mix64(seed + detail::kGolden * (stream + 1));
}

// wrapCoordinate without its division when v already lies in the canonical range
// [-period / 2, period / 2), as most sampled coordinates do: there wrapCoordinate gives v itself,
// bit for bit. Its k = floor(v / period + 0.5) is +0 there, or 1 when v / period rounds up to
// 0.5, which the comparison with the exact bound period - period / 2 brings back to +0; and
// v - (+0) * period is v, -0 included. Anything else, NaN and infinities included, takes
// wrapCoordinate.
[[nodiscard]] inline double wrapCanonical(double v, double period) {
    const double half = 0.5 * period;
    if (v >= -half && v < half) {
        return v;
    }
    return wrapCoordinate(v, period);
}

// Every noise tiles the world (WorldSize): it is periodic with period world.x along x and
// world.z along z, exactly continuous across the seam, and gives the same bits at (x, z) and
// at any image (x + k world.x, z + l world.z) of an exactly representable point, as the
// coordinates are brought back into the canonical range first (wrapCoordinate).
//
// The octaves of a noise are rotated so that their lattices never line up, by exact rotations
// (a / c, b / c) of a Pythagorean triple a^2 + b^2 = c^2: (1, 0, 1) (none), (4, 3, 5),
// (5, 12, 13), (24, 7, 25), (3, 4, 5), (15, 8, 17), (7, 24, 25), (12, 5, 13), octave k taking
// rotation k modulo kRotationCount. Rotations of 3D noises are horizontal: one mixing y into x
// and z would break the period along x and z.
inline constexpr int kRotationCount = 8;

// A lattice of gradient noise that tiles the world, in the horizontal plane. Along x it has
// cellsX = c * max(1, round(world.x * frequency / c)) cells per period, likewise along z: as
// every grid of the world (PeriodicCells), a whole number of its super-cells of c x c cells
// falls on the period, the frequency adjusted to fit (by at most c / (2 world.x frequency),
// relative: 0.1 % for a wavelength of 120 km without rotation). The lattice is rotated by
// (a / c, b / c) and moved by (offsetU, offsetV) cells; the periods of the world map to integer
// vectors of the rotated lattice, whose points are hashed through two invariants of the period
// lattice, (a u + b v) mod (cellsX c) and (a v - b u) mod (cellsZ c): every image of a lattice
// point gets the same gradient.
struct Lattice2D {
    double scaleX = 1.0; // cells per meter along x, cellsX / world.x
    double scaleZ = 1.0;
    std::int64_t a = 1;
    std::int64_t b = 0;
    std::int64_t c = 1;
    std::int64_t periodS = 1; // cellsX * c
    std::int64_t periodT = 1; // cellsZ * c
    double offsetU = 0.0;
    double offsetV = 0.0;
    WorldSize world;
    // Rotated lattices of OctaveLattices::RotatedFast multiply by inverseC = 1 / c instead of
    // dividing by c: other bits, faster.
    bool reciprocal = false;
    double inverseC = 1.0;

    [[nodiscard]] std::int64_t cellsX() const { return periodS / c; }
    [[nodiscard]] std::int64_t cellsZ() const { return periodT / c; }
};

// Lattice of frequency cells per meter (before the adjustment), with rotation number rotation
// (0 for none) and moved by (offsetU, offsetV) cells. The frequency must be finite and above 0.
[[nodiscard]] Lattice2D lattice2D(const WorldSize& world, double frequency, int rotation = 0,
                                  double offsetU = 0.0, double offsetV = 0.0);

// 2D gradient (Perlin) noise of a lattice at (x, z) meters: one of 16 unit gradients hashed
// from (invariants, seed) at each lattice point, without a permutation table. Range [-1, 1],
// exactly 0 on the points of the lattice, C2 continuous (quintic fade).
[[nodiscard]] float gradient2D(double x, double z, std::uint64_t seed, const Lattice2D& lattice);

// Lattices of the octaves of a noise. Rotated, the default, is the lattice of every noise up to
// generator 2 and keeps its bits. The others give other bits, faster, for the layers that can
// change (the planetary geography of phase 11R):
// - RotatedFast: the same rotations, multiplying by 1 / c instead of dividing by c;
// - Unrotated: no rotation, octave k moved by (k phi, k sqrt 2) modulo 1 cells, so that the
//   octaves do not share the zeros of their lattices. Rotation brings nothing to the
//   planetary layers, whose few cells along a period it bends by up to a quarter to fit whole
//   super-cells, nor to faint detail.
enum class OctaveLattices : std::uint8_t { Rotated, RotatedFast, Unrotated };

struct FbmParams {
    int octaves = 4;
    double frequency = 1.0;  // of the first octave, in cycles per meter
    double lacunarity = 2.0; // frequency ratio between consecutive octaves
    double gain = 0.5;       // amplitude ratio between consecutive octaves
    OctaveLattices lattices = OctaveLattices::Rotated;
};

// The lattice of octave octave of a noise of frequency frequency (that octave's): lattice2D
// with rotation octave for Rotated, the bits of every noise before them.
[[nodiscard]] Lattice2D octaveLattice(const WorldSize& world, double frequency, int octave,
                                      OctaveLattices lattices);

// Fractal Brownian motion: octave k is gradient2D of frequency frequency * lacunarity^k on
// octaveLattice(world, that frequency, k, params.lattices), with its own seed. Normalized by the
// total amplitude: range [-1, 1].
[[nodiscard]] float fbm2D(double x, double z, std::uint64_t seed, const FbmParams& params,
                          const WorldSize& world);

// Ridged multifractal (Musgrave): each octave is (1 - |n|)^sharpness, peaking on the zero
// lines of the noise, and is weighted by the previous one so detail stays on the ridges.
// A higher sharpness gives narrower crests and lower ground between them.
// Range [0, 1]. Terrain must only use it multiplied by a mountain mask.
[[nodiscard]] float ridged2D(double x, double z, std::uint64_t seed, const FbmParams& params,
                             const WorldSize& world, double sharpness = 2.0);

// 0 at or below edge0, 1 at or above edge1, Hermite curve in between. Requires edge0 < edge1.
[[nodiscard]] float smoothstep(float edge0, float edge1, float x);

// 3D gradient (Perlin) noise: the lattice in the horizontal plane, frequencyY cells per meter
// along y (not periodic) moved by offsetY cells. Each lattice point picks one of 16 unit
// gradients (the 12 edge directions of the cube, 4 of them repeated) hashed from (invariants,
// y cell, seed). Range [-1, 1], exactly 0 on the points of the lattice, C2 continuous.
[[nodiscard]] float gradient3D(double x, double y, double z, std::uint64_t seed,
                               const Lattice2D& lattice, double frequencyY, double offsetY = 0.0);

// Fractal Brownian motion of gradient3D: octave k with frequency * lacunarity^k along every
// axis (adjusted horizontally), rotation k and a fixed offset along y, so that the lattices of
// the octaves never line up. Normalized by the total amplitude: range [-1, 1].
[[nodiscard]] float fbm3D(double x, double y, double z, std::uint64_t seed, const FbmParams& params,
                          const WorldSize& world);

} // namespace terrain::noise

#pragma once

// FROZEN REFERENCE, do not edit: the noise and the wrapping of terrain_core as of commit 052078b,
// before the noise performance work (frozen_noise.cpp). The live code must give the same bits:
// frozen_reference_test compares them. The data types (Lattice2D, FbmParams, WorldSize) are
// those of terrain_core, whose layout the optimizations do not change.

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/world_size.hpp"

#include <cmath>
#include <cstdint>

namespace frozen {

// The copies call terrain::dmath as dmath, as the originals do from namespace terrain.
namespace dmath = terrain::dmath;

// terrain::wrapCoordinate (terrain_core/src/world_size.cpp), verbatim.
inline double wrapCoordinate(double v, double period) {
    // The bounds k * period +- period / 2 are exact (period is a multiple of 2^16), so comparing
    // v with them corrects any rounding of v / period; then v - k * period is exact, as v and
    // k * period are multiples of the unit in the last place of v and the result is smaller.
    const double half = 0.5 * period;
    double k = std::floor(v / period + 0.5);
    if (v < k * period - half) {
        k -= 1.0;
    } else if (v >= k * period + half) {
        k += 1.0;
    }
    return v - k * period;
}

namespace noise {

using terrain::noise::FbmParams;
using terrain::noise::kRotationCount;
using terrain::noise::Lattice2D;

[[nodiscard]] std::uint64_t deriveSeed(std::uint64_t seed, std::uint64_t stream);
[[nodiscard]] terrain::noise::Lattice2D lattice2D(const terrain::WorldSize& world,
                                                  double frequency, int rotation = 0,
                                                  double offsetU = 0.0, double offsetV = 0.0);
[[nodiscard]] float gradient2D(double x, double z, std::uint64_t seed,
                               const terrain::noise::Lattice2D& lattice);
[[nodiscard]] float fbm2D(double x, double z, std::uint64_t seed,
                          const terrain::noise::FbmParams& params, const terrain::WorldSize& world);
[[nodiscard]] float ridged2D(double x, double z, std::uint64_t seed,
                             const terrain::noise::FbmParams& params,
                             const terrain::WorldSize& world, double sharpness = 2.0);
[[nodiscard]] float smoothstep(float edge0, float edge1, float x);
[[nodiscard]] float gradient3D(double x, double y, double z, std::uint64_t seed,
                               const terrain::noise::Lattice2D& lattice, double frequencyY,
                               double offsetY = 0.0);
[[nodiscard]] float fbm3D(double x, double y, double z, std::uint64_t seed,
                          const terrain::noise::FbmParams& params, const terrain::WorldSize& world);

} // namespace noise

} // namespace frozen

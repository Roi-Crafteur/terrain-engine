#pragma once

#include <cstdint>

namespace terrain::noise {

// Independent seed for a layer or an octave of a generator (splitmix64 mixing).
[[nodiscard]] std::uint64_t deriveSeed(std::uint64_t seed, std::uint64_t stream);

// 2D gradient (Perlin) noise, one lattice cell per unit of x and z. Gradients are hashed
// from (cell, seed) without a permutation table, so the noise never repeats.
// Range [-1, 1], exactly 0 on integer lattice points, C2 continuous (quintic fade).
[[nodiscard]] float gradient2D(double x, double z, std::uint64_t seed);

struct FbmParams {
    int octaves = 4;
    double frequency = 1.0;  // of the first octave, in cycles per unit of x and z
    double lacunarity = 2.0; // frequency ratio between consecutive octaves
    double gain = 0.5;       // amplitude ratio between consecutive octaves
};

// Fractal Brownian motion: octaves of gradient2D, each with its own seed and rotated to
// hide the lattice. Normalized by the total amplitude: range [-1, 1].
[[nodiscard]] float fbm2D(double x, double z, std::uint64_t seed, const FbmParams& params);

// Ridged multifractal (Musgrave): each octave is (1 - |n|)^sharpness, peaking on the zero
// lines of the noise, and is weighted by the previous one so detail stays on the ridges.
// A higher sharpness gives narrower crests and lower ground between them.
// Range [0, 1]. Terrain must only use it multiplied by a mountain mask.
[[nodiscard]] float ridged2D(double x, double z, std::uint64_t seed, const FbmParams& params,
                             double sharpness = 2.0);

// 0 at or below edge0, 1 at or above edge1, Hermite curve in between. Requires edge0 < edge1.
[[nodiscard]] float smoothstep(float edge0, float edge1, float x);

} // namespace terrain::noise

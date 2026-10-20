#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace terrain::noise {

namespace {

constexpr std::uint64_t kGolden = 0x9E3779B97F4A7C15ULL;
constexpr std::uint64_t kCellZPrime = 0xC2B2AE3D27D4EB4FULL;
constexpr std::uint64_t kCellYPrime = 0xD6E8FEB86659FD93ULL;

// splitmix64 finalizer: every input bit affects every output bit.
constexpr std::uint64_t mix64(std::uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// 16 unit gradients evenly spaced on the circle.
constexpr double kCos22 = 0.92387953251128674;
constexpr double kSin22 = 0.38268343236508978;
constexpr double kDiag = 0.70710678118654752;
constexpr std::array<std::array<double, 2>, 16> kGradients = {{
    {1.0, 0.0},       {kCos22, kSin22},   {kDiag, kDiag},   {kSin22, kCos22},
    {0.0, 1.0},       {-kSin22, kCos22},  {-kDiag, kDiag},  {-kCos22, kSin22},
    {-1.0, 0.0},      {-kCos22, -kSin22}, {-kDiag, -kDiag}, {-kSin22, -kCos22},
    {0.0, -1.0},      {kSin22, -kCos22},  {kDiag, -kDiag},  {kCos22, -kSin22},
}};

// Dot product of the gradient of lattice point (ix, iz) with the offset (dx, dz) to it.
double cornerDot(std::int64_t ix, std::int64_t iz, double dx, double dz, std::uint64_t seed) {
    const std::uint64_t h = mix64(mix64(seed ^ (static_cast<std::uint64_t>(ix) * kGolden)) ^
                                  (static_cast<std::uint64_t>(iz) * kCellZPrime));
    const auto& g = kGradients[h & 15U];
    return g[0] * dx + g[1] * dz;
}

constexpr double fade(double t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

// Octave coordinates are rotated by atan(3/4) (about 37 degrees), exact in floating point,
// and scaled by the lacunarity so that octave lattices never line up.
void nextOctave(double& x, double& z, double lacunarity) {
    const double rx = 0.8 * x - 0.6 * z;
    const double rz = 0.6 * x + 0.8 * z;
    x = rx * lacunarity;
    z = rz * lacunarity;
}

// The 12 edge directions of the cube (+-1, +-1, 0), (+-1, 0, +-1), (0, +-1, +-1), then 4 of
// them again to fill 16 entries, all scaled to unit length.
constexpr std::array<std::array<double, 3>, 16> kGradients3 = {{
    {kDiag, kDiag, 0.0},   {-kDiag, kDiag, 0.0},  {kDiag, -kDiag, 0.0},  {-kDiag, -kDiag, 0.0},
    {kDiag, 0.0, kDiag},   {-kDiag, 0.0, kDiag},  {kDiag, 0.0, -kDiag},  {-kDiag, 0.0, -kDiag},
    {0.0, kDiag, kDiag},   {0.0, -kDiag, kDiag},  {0.0, kDiag, -kDiag},  {0.0, -kDiag, -kDiag},
    {kDiag, kDiag, 0.0},   {-kDiag, kDiag, 0.0},  {0.0, -kDiag, kDiag},  {0.0, -kDiag, -kDiag},
}};

// Dot product of the gradient of lattice point (ix, iy, iz) with the offset (dx, dy, dz) to it.
double cornerDot3(std::int64_t ix, std::int64_t iy, std::int64_t iz, double dx, double dy,
                  double dz, std::uint64_t seed) {
    const std::uint64_t h =
        mix64(mix64(mix64(seed ^ (static_cast<std::uint64_t>(ix) * kGolden)) ^
                    (static_cast<std::uint64_t>(iy) * kCellYPrime)) ^
              (static_cast<std::uint64_t>(iz) * kCellZPrime));
    const auto& g = kGradients3[h & 15U];
    return g[0] * dx + g[1] * dy + g[2] * dz;
}

// Octave coordinates are rotated by the exact orthogonal matrix (1/3) * [[2, -1, 2],
// [2, 2, -1], [-1, 2, 2]] (a rotation whose entries are small rationals) and scaled by the
// lacunarity so that octave lattices never line up.
void nextOctave3D(double& x, double& y, double& z, double lacunarity) {
    const double rx = (2.0 * x - y + 2.0 * z) / 3.0;
    const double ry = (2.0 * x + 2.0 * y - z) / 3.0;
    const double rz = (-x + 2.0 * y + 2.0 * z) / 3.0;
    x = rx * lacunarity;
    y = ry * lacunarity;
    z = rz * lacunarity;
}

} // namespace

std::uint64_t deriveSeed(std::uint64_t seed, std::uint64_t stream) {
    return mix64(seed + kGolden * (stream + 1));
}

float gradient2D(double x, double z, std::uint64_t seed) {
    const double x0 = std::floor(x);
    const double z0 = std::floor(z);
    const auto ix = static_cast<std::int64_t>(x0);
    const auto iz = static_cast<std::int64_t>(z0);
    const double fx = x - x0;
    const double fz = z - z0;

    const double n00 = cornerDot(ix, iz, fx, fz, seed);
    const double n10 = cornerDot(ix + 1, iz, fx - 1.0, fz, seed);
    const double n01 = cornerDot(ix, iz + 1, fx, fz - 1.0, seed);
    const double n11 = cornerDot(ix + 1, iz + 1, fx - 1.0, fz - 1.0, seed);
    const double u = fade(fx);
    const double v = fade(fz);
    const double nz0 = n00 + u * (n10 - n00);
    const double nz1 = n01 + u * (n11 - n01);
    // 2D Perlin noise with unit gradients stays within +-sqrt(2) / 2.
    const double value = (nz0 + v * (nz1 - nz0)) * std::numbers::sqrt2;
    return static_cast<float>(std::clamp(value, -1.0, 1.0));
}

float fbm2D(double x, double z, std::uint64_t seed, const FbmParams& params) {
    double px = x * params.frequency;
    double pz = z * params.frequency;
    double amplitude = 1.0;
    double sum = 0.0;
    double total = 0.0;
    for (int octave = 0; octave < params.octaves; ++octave) {
        const auto octaveSeed = deriveSeed(seed, static_cast<std::uint64_t>(octave));
        sum += amplitude * gradient2D(px, pz, octaveSeed);
        total += amplitude;
        amplitude *= params.gain;
        nextOctave(px, pz, params.lacunarity);
    }
    return total > 0.0 ? static_cast<float>(std::clamp(sum / total, -1.0, 1.0)) : 0.0f;
}

float ridged2D(double x, double z, std::uint64_t seed, const FbmParams& params,
               double sharpness) {
    double px = x * params.frequency;
    double pz = z * params.frequency;
    double amplitude = 1.0;
    double weight = 1.0;
    double sum = 0.0;
    double total = 0.0;
    for (int octave = 0; octave < params.octaves; ++octave) {
        const auto octaveSeed = deriveSeed(seed, static_cast<std::uint64_t>(octave));
        const double ridge = 1.0 - std::abs(static_cast<double>(gradient2D(px, pz, octaveSeed)));
        const double signal = std::pow(ridge, sharpness) * weight;
        weight = std::clamp(2.0 * signal, 0.0, 1.0);
        sum += amplitude * signal;
        total += amplitude;
        amplitude *= params.gain;
        nextOctave(px, pz, params.lacunarity);
    }
    return total > 0.0 ? static_cast<float>(std::clamp(sum / total, 0.0, 1.0)) : 0.0f;
}

float smoothstep(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float gradient3D(double x, double y, double z, std::uint64_t seed) {
    const double x0 = std::floor(x);
    const double y0 = std::floor(y);
    const double z0 = std::floor(z);
    const auto ix = static_cast<std::int64_t>(x0);
    const auto iy = static_cast<std::int64_t>(y0);
    const auto iz = static_cast<std::int64_t>(z0);
    const double fx = x - x0;
    const double fy = y - y0;
    const double fz = z - z0;

    const double n000 = cornerDot3(ix, iy, iz, fx, fy, fz, seed);
    const double n100 = cornerDot3(ix + 1, iy, iz, fx - 1.0, fy, fz, seed);
    const double n010 = cornerDot3(ix, iy + 1, iz, fx, fy - 1.0, fz, seed);
    const double n110 = cornerDot3(ix + 1, iy + 1, iz, fx - 1.0, fy - 1.0, fz, seed);
    const double n001 = cornerDot3(ix, iy, iz + 1, fx, fy, fz - 1.0, seed);
    const double n101 = cornerDot3(ix + 1, iy, iz + 1, fx - 1.0, fy, fz - 1.0, seed);
    const double n011 = cornerDot3(ix, iy + 1, iz + 1, fx, fy - 1.0, fz - 1.0, seed);
    const double n111 = cornerDot3(ix + 1, iy + 1, iz + 1, fx - 1.0, fy - 1.0, fz - 1.0, seed);
    const double u = fade(fx);
    const double v = fade(fy);
    const double w = fade(fz);
    const double nx00 = n000 + u * (n100 - n000);
    const double nx10 = n010 + u * (n110 - n010);
    const double nx01 = n001 + u * (n101 - n001);
    const double nx11 = n011 + u * (n111 - n011);
    const double ny0 = nx00 + v * (nx10 - nx00);
    const double ny1 = nx01 + v * (nx11 - nx01);
    // 3D Perlin noise with unit gradients stays within +-sqrt(3) / 2.
    const double value = (ny0 + w * (ny1 - ny0)) * (2.0 * std::numbers::inv_sqrt3);
    return static_cast<float>(std::clamp(value, -1.0, 1.0));
}

float fbm3D(double x, double y, double z, std::uint64_t seed, const FbmParams& params) {
    double px = x * params.frequency;
    double py = y * params.frequency;
    double pz = z * params.frequency;
    double amplitude = 1.0;
    double sum = 0.0;
    double total = 0.0;
    for (int octave = 0; octave < params.octaves; ++octave) {
        const auto octaveSeed = deriveSeed(seed, static_cast<std::uint64_t>(octave));
        sum += amplitude * gradient3D(px, py, pz, octaveSeed);
        total += amplitude;
        amplitude *= params.gain;
        nextOctave3D(px, py, pz, params.lacunarity);
    }
    return total > 0.0 ? static_cast<float>(std::clamp(sum / total, -1.0, 1.0)) : 0.0f;
}

} // namespace terrain::noise

#include "terrain/generation/noise.hpp"

#include "terrain/det_math.hpp"
#include "noise_lattice.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace terrain::noise {

namespace {

using detail::kGolden;
using detail::kOffsetsY;
using detail::latticeNoise;
using detail::latticeNoise3;
using detail::mix64;

// Exact rotations (a / c, b / c) of the octaves: see kRotationCount.
struct Rotation {
    std::int64_t a;
    std::int64_t b;
    std::int64_t c;
};
constexpr std::array<Rotation, kRotationCount> kRotations = {{
    {1, 0, 1},
    {4, 3, 5},
    {5, 12, 13},
    {24, 7, 25},
    {3, 4, 5},
    {15, 8, 17},
    {7, 24, 25},
    {12, 5, 13},
}};

// Cells along a period of the world: a whole number of super-cells of c cells.
std::int64_t cellsOn(double period, double frequency, std::int64_t c) {
    const double superCells =
        std::max(1.0, std::round(period * frequency / static_cast<double>(c)));
    return static_cast<std::int64_t>(superCells) * c;
}

// Offset of octave k of Unrotated lattices along one axis, in cells: the fractional part of
// k times an irrational step (the golden ratio, sqrt 2), 0 for the first octave.
double unrotatedOffset(int octave, double step) {
    const double v = static_cast<double>(octave) * step;
    return v - std::floor(v);
}

} // namespace

Lattice2D octaveLattice(const WorldSize& world, double frequency, int octave,
                        OctaveLattices lattices) {
    if (lattices == OctaveLattices::Unrotated) {
        return lattice2D(world, frequency, 0, unrotatedOffset(octave, 0.6180339887498949),
                         unrotatedOffset(octave, 0.4142135623730951));
    }
    Lattice2D lattice = lattice2D(world, frequency, octave);
    if (lattices == OctaveLattices::RotatedFast && lattice.c != 1) {
        lattice.reciprocal = true;
        lattice.inverseC = 1.0 / static_cast<double>(lattice.c);
    }
    return lattice;
}

std::uint64_t deriveSeed(std::uint64_t seed, std::uint64_t stream) {
    return mix64(seed + kGolden * (stream + 1));
}

Lattice2D lattice2D(const WorldSize& world, double frequency, int rotation, double offsetU,
                    double offsetV) {
    const Rotation& r = kRotations[static_cast<std::size_t>(rotation % kRotationCount)];
    const std::int64_t cellsX = cellsOn(world.x, frequency, r.c);
    const std::int64_t cellsZ = cellsOn(world.z, frequency, r.c);
    return {static_cast<double>(cellsX) / world.x,
            static_cast<double>(cellsZ) / world.z,
            r.a,
            r.b,
            r.c,
            cellsX * r.c,
            cellsZ * r.c,
            offsetU,
            offsetV,
            world};
}

float gradient2D(double x, double z, std::uint64_t seed, const Lattice2D& lattice) {
    return latticeNoise(wrapCanonical(x, lattice.world.x), wrapCanonical(z, lattice.world.z),
                        seed, lattice);
}

float fbm2D(double x, double z, std::uint64_t seed, const FbmParams& params,
            const WorldSize& world) {
    const double cx = wrapCanonical(x, world.x);
    const double cz = wrapCanonical(z, world.z);
    double frequency = params.frequency;
    double amplitude = 1.0;
    double sum = 0.0;
    double total = 0.0;
    for (int octave = 0; octave < params.octaves; ++octave) {
        const auto octaveSeed = deriveSeed(seed, static_cast<std::uint64_t>(octave));
        sum += amplitude * latticeNoise(cx, cz, octaveSeed,
                                        octaveLattice(world, frequency, octave, params.lattices));
        total += amplitude;
        amplitude *= params.gain;
        frequency *= params.lacunarity;
    }
    return total > 0.0 ? static_cast<float>(std::clamp(sum / total, -1.0, 1.0)) : 0.0f;
}

float ridged2D(double x, double z, std::uint64_t seed, const FbmParams& params,
               const WorldSize& world, double sharpness) {
    const double cx = wrapCanonical(x, world.x);
    const double cz = wrapCanonical(z, world.z);
    double frequency = params.frequency;
    double amplitude = 1.0;
    double weight = 1.0;
    double sum = 0.0;
    double total = 0.0;
    for (int octave = 0; octave < params.octaves; ++octave) {
        const auto octaveSeed = deriveSeed(seed, static_cast<std::uint64_t>(octave));
        const float n = latticeNoise(cx, cz, octaveSeed,
                                     octaveLattice(world, frequency, octave, params.lattices));
        const double ridge = 1.0 - std::abs(static_cast<double>(n));
        const double signal = dmath::pow(ridge, sharpness) * weight;
        weight = std::clamp(2.0 * signal, 0.0, 1.0);
        sum += amplitude * signal;
        total += amplitude;
        amplitude *= params.gain;
        frequency *= params.lacunarity;
    }
    return total > 0.0 ? static_cast<float>(std::clamp(sum / total, 0.0, 1.0)) : 0.0f;
}

float smoothstep(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float gradient3D(double x, double y, double z, std::uint64_t seed, const Lattice2D& lattice,
                 double frequencyY, double offsetY) {
    return latticeNoise3(wrapCanonical(x, lattice.world.x), y, wrapCanonical(z, lattice.world.z),
                         seed, lattice, frequencyY, offsetY);
}

float fbm3D(double x, double y, double z, std::uint64_t seed, const FbmParams& params,
            const WorldSize& world) {
    const double cx = wrapCanonical(x, world.x);
    const double cz = wrapCanonical(z, world.z);
    double frequency = params.frequency;
    double amplitude = 1.0;
    double sum = 0.0;
    double total = 0.0;
    for (int octave = 0; octave < params.octaves; ++octave) {
        const auto octaveSeed = deriveSeed(seed, static_cast<std::uint64_t>(octave));
        const double offsetY = kOffsetsY[static_cast<std::size_t>(octave % kRotationCount)];
        sum += amplitude *
               latticeNoise3(cx, y, cz, octaveSeed,
                             octaveLattice(world, frequency, octave, params.lattices), frequency,
                             offsetY);
        total += amplitude;
        amplitude *= params.gain;
        frequency *= params.lacunarity;
    }
    return total > 0.0 ? static_cast<float>(std::clamp(sum / total, -1.0, 1.0)) : 0.0f;
}

} // namespace terrain::noise

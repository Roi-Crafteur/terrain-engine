#include "terrain/generation/noise_octaves.hpp"

#include "terrain/det_math.hpp"
#include "noise_lattice.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace terrain::noise {

// The same sequence as the loops of fbm2D, ridged2D and fbm3D: the same doubles.
FbmOctaves::FbmOctaves(std::uint64_t seed, const FbmParams& params, const WorldSize& world)
    : world_(world) {
    double frequency = params.frequency;
    double amplitude = 1.0;
    for (int octave = 0; octave < params.octaves; ++octave) {
        octaves_.push_back(
            {deriveSeed(seed, static_cast<std::uint64_t>(octave)),
             lattice2D(world, frequency, octave), amplitude, frequency,
             detail::kOffsetsY[static_cast<std::size_t>(octave % kRotationCount)]});
        total_ += amplitude;
        amplitude *= params.gain;
        frequency *= params.lacunarity;
    }
}

// Without octave, or with a total amplitude of at most 0, the free functions ignore their sum
// and give 0: so do these, without computing it.
float FbmOctaves::fbm2D(double x, double z) const {
    if (!(total_ > 0.0)) {
        return 0.0f;
    }
    const double cx = wrapCanonical(x, world_.x);
    const double cz = wrapCanonical(z, world_.z);
    double sum = 0.0;
    for (const Octave& o : octaves_) {
        sum += o.amplitude * detail::latticeNoise(cx, cz, o.seed, o.lattice);
    }
    return static_cast<float>(std::clamp(sum / total_, -1.0, 1.0));
}

float FbmOctaves::ridged2D(double x, double z, double sharpness) const {
    if (!(total_ > 0.0)) {
        return 0.0f;
    }
    const double cx = wrapCanonical(x, world_.x);
    const double cz = wrapCanonical(z, world_.z);
    double weight = 1.0;
    double sum = 0.0;
    for (const Octave& o : octaves_) {
        const float n = detail::latticeNoise(cx, cz, o.seed, o.lattice);
        const double ridge = 1.0 - std::abs(static_cast<double>(n));
        const double signal = dmath::pow(ridge, sharpness) * weight;
        weight = std::clamp(2.0 * signal, 0.0, 1.0);
        sum += o.amplitude * signal;
    }
    return static_cast<float>(std::clamp(sum / total_, 0.0, 1.0));
}

float FbmOctaves::fbm3D(double x, double y, double z) const {
    if (!(total_ > 0.0)) {
        return 0.0f;
    }
    const double cx = wrapCanonical(x, world_.x);
    const double cz = wrapCanonical(z, world_.z);
    double sum = 0.0;
    for (const Octave& o : octaves_) {
        sum += o.amplitude *
               detail::latticeNoise3(cx, y, cz, o.seed, o.lattice, o.frequency, o.offsetY);
    }
    return static_cast<float>(std::clamp(sum / total_, -1.0, 1.0));
}

} // namespace terrain::noise

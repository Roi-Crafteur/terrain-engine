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
template <class Noise>
float FbmOctaves::fbm2DWith(double x, double z, Noise&& noise) const {
    if (!(total_ > 0.0)) {
        return 0.0f;
    }
    const double cx = wrapCanonical(x, world_.x);
    const double cz = wrapCanonical(z, world_.z);
    double sum = 0.0;
    for (std::size_t i = 0; i < octaves_.size(); ++i) {
        sum += octaves_[i].amplitude * noise(i, octaves_[i], cx, cz);
    }
    return static_cast<float>(std::clamp(sum / total_, -1.0, 1.0));
}

template <class Noise>
float FbmOctaves::ridged2DWith(double x, double z, double sharpness, Noise&& noise) const {
    if (!(total_ > 0.0)) {
        return 0.0f;
    }
    const double cx = wrapCanonical(x, world_.x);
    const double cz = wrapCanonical(z, world_.z);
    double weight = 1.0;
    double sum = 0.0;
    for (std::size_t i = 0; i < octaves_.size(); ++i) {
        const float n = noise(i, octaves_[i], cx, cz);
        const double ridge = 1.0 - std::abs(static_cast<double>(n));
        const double signal = dmath::pow(ridge, sharpness) * weight;
        weight = std::clamp(2.0 * signal, 0.0, 1.0);
        sum += octaves_[i].amplitude * signal;
    }
    return static_cast<float>(std::clamp(sum / total_, 0.0, 1.0));
}

namespace {

// The noise of an octave, straight or through the memo of its cells.
constexpr auto kStraight = [](std::size_t, const auto& o, double x, double z) {
    return detail::latticeNoise(x, z, o.seed, o.lattice);
};

auto throughMemo(FbmOctaves::Memo& memo) {
    return [&memo](std::size_t i, const auto& o, double x, double z) {
        return i < memo.cells.size() ? detail::latticeNoise(x, z, o.seed, o.lattice, memo.cells[i])
                                     : detail::latticeNoise(x, z, o.seed, o.lattice);
    };
}

} // namespace

float FbmOctaves::fbm2D(double x, double z) const {
    return fbm2DWith(x, z, kStraight);
}

float FbmOctaves::fbm2D(double x, double z, Memo& memo) const {
    return fbm2DWith(x, z, throughMemo(memo));
}

float FbmOctaves::ridged2D(double x, double z, double sharpness) const {
    return ridged2DWith(x, z, sharpness, kStraight);
}

float FbmOctaves::ridged2D(double x, double z, double sharpness, Memo& memo) const {
    return ridged2DWith(x, z, sharpness, throughMemo(memo));
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

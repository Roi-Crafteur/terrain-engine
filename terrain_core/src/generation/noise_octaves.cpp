#include "terrain/generation/noise_octaves.hpp"

#include "terrain/det_math.hpp"
#include "noise_lattice.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

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

bool sameBits(double a, double b) {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

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

bool FbmOctaves::twinOf(const FbmOctaves& twin) const {
    if (octaves_.size() != twin.octaves_.size() || !sameBits(total_, twin.total_) ||
        !(world_ == twin.world_)) {
        return false;
    }
    for (std::size_t i = 0; i < octaves_.size(); ++i) {
        const Octave& a = octaves_[i];
        const Octave& b = twin.octaves_[i];
        const Lattice2D& la = a.lattice;
        const Lattice2D& lb = b.lattice;
        if (!sameBits(a.amplitude, b.amplitude) || !sameBits(la.scaleX, lb.scaleX) ||
            !sameBits(la.scaleZ, lb.scaleZ) || la.a != lb.a || la.b != lb.b || la.c != lb.c ||
            la.periodS != lb.periodS || la.periodT != lb.periodT ||
            !sameBits(la.offsetU, lb.offsetU) || !sameBits(la.offsetV, lb.offsetV) ||
            !(la.world == lb.world)) {
            return false;
        }
    }
    return true;
}

// The two sums of fbm2DWith, each in its own order, the position computed once per octave.
template <class Noise>
std::array<float, 2> FbmOctaves::pairWith(const FbmOctaves& twin, double x, double z,
                                          Noise&& noise) const {
    if (!(total_ > 0.0)) {
        return {0.0f, 0.0f};
    }
    const double cx = wrapCanonical(x, world_.x);
    const double cz = wrapCanonical(z, world_.z);
    double sum = 0.0;
    double twinSum = 0.0;
    for (std::size_t i = 0; i < octaves_.size(); ++i) {
        const Octave& o = octaves_[i];
        const detail::CellPosition c = detail::positionOf(detail::toLattice(cx, cz, o.lattice));
        const std::array<float, 2> n = noise(i, o, twin.octaves_[i], c);
        sum += o.amplitude * n[0];
        twinSum += o.amplitude * n[1];
    }
    return {static_cast<float>(std::clamp(sum / total_, -1.0, 1.0)),
            static_cast<float>(std::clamp(twinSum / total_, -1.0, 1.0))};
}

std::array<float, 2> FbmOctaves::fbm2DPair(const FbmOctaves& twin, double x, double z) const {
    return pairWith(twin, x, z, [](std::size_t, const auto& o, const auto& t, const auto& c) {
        return std::array<float, 2>{detail::noiseAt(c, o.seed, o.lattice),
                                    detail::noiseAt(c, t.seed, t.lattice)};
    });
}

std::array<float, 2> FbmOctaves::fbm2DPair(const FbmOctaves& twin, double x, double z, Memo& memo,
                                           Memo& twinMemo) const {
    return pairWith(twin, x, z, [&](std::size_t i, const auto& o, const auto& t, const auto& c) {
        if (i >= memo.cells.size()) {
            return std::array<float, 2>{detail::noiseAt(c, o.seed, o.lattice),
                                        detail::noiseAt(c, t.seed, t.lattice)};
        }
        return std::array<float, 2>{detail::noiseAt(c, o.seed, o.lattice, memo.cells[i]),
                                    detail::noiseAt(c, t.seed, t.lattice, twinMemo.cells[i])};
    });
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

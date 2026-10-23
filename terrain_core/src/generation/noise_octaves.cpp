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
             octaveLattice(world, frequency, octave, params.lattices), amplitude, frequency,
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

// The sums of ridged2DWith in the same order, plus those of the head octaves with their
// derivatives: the signal pow(1 - |n|, sharpness) * weight, the weight of the next octave
// clamp(2 signal, floor, 1). The derivatives take the weights as they are, without their own
// slope: they turn over only on the creases, not where a weight reaches its bounds.
template <class Cells>
FbmOctaves::RidgedSlope FbmOctaves::ridgedSlopeWith(double x, double z, double sharpness,
                                                    double floor, int head,
                                                    Cells&& cells) const {
    RidgedSlope out;
    if (!(total_ > 0.0)) {
        return out;
    }
    const double cx = wrapCanonical(x, world_.x);
    const double cz = wrapCanonical(z, world_.z);
    double weight = 1.0;
    double sum = 0.0;
    double headTotal = 0.0;
    double headSum = 0.0;
    double headX = 0.0;
    double headZ = 0.0;
    for (std::size_t i = 0; i < octaves_.size(); ++i) {
        const Octave& o = octaves_[i];
        const detail::CellPosition c = detail::positionOf(detail::toLattice(cx, cz, o.lattice));
        const CellGradients& g = cells(i, o, c);
        if (static_cast<int>(i) >= head) {
            const float n = detail::cellNoise(g, c.fu, c.fv);
            const double signal =
                dmath::pow(1.0 - std::abs(static_cast<double>(n)), sharpness) * weight;
            weight = std::clamp(2.0 * signal, floor, 1.0);
            sum += o.amplitude * signal;
            continue;
        }
        const detail::NoiseSlope n = detail::cellNoiseSlope(g, c.fu, c.fv);
        out.crease = std::min(out.crease, std::abs(n.value));
        const double ridge = 1.0 - std::abs(static_cast<double>(n.value));
        const double powered = dmath::pow(ridge, sharpness);
        const double signal = powered * weight;
        // d signal = d powered * weight, d ridge = -sign(n) d n.
        const std::array<double, 4> j = detail::latticeJacobian(o.lattice);
        const double side = n.value > 0.0f ? -1.0 : (n.value < 0.0f ? 1.0 : 0.0);
        const double rise = ridge > 0.0 ? sharpness * powered / ridge * side * weight : 0.0;
        const double signalX = rise * (n.du * j[0] + n.dv * j[2]);
        const double signalZ = rise * (n.du * j[1] + n.dv * j[3]);
        weight = std::clamp(2.0 * signal, floor, 1.0);
        sum += o.amplitude * signal;
        headTotal += o.amplitude;
        headSum += o.amplitude * signal;
        headX += o.amplitude * signalX;
        headZ += o.amplitude * signalZ;
    }
    out.value = static_cast<float>(std::clamp(sum / total_, 0.0, 1.0));
    if (headTotal > 0.0) {
        const double h = headSum / headTotal;
        out.head = static_cast<float>(std::clamp(h, 0.0, 1.0));
        out.headX = headX / headTotal;
        out.headZ = headZ / headTotal;
    }
    return out;
}

FbmOctaves::RidgedSlope FbmOctaves::ridged2DSlope(double x, double z, double sharpness,
                                                  double floor, int head) const {
    CellGradients g{};
    return ridgedSlopeWith(x, z, sharpness, floor, head,
                           [&g](std::size_t, const Octave& o, const detail::CellPosition& c)
                               -> const CellGradients& {
                               g = detail::gradientsOf(c.iu, c.iv, o.seed, o.lattice);
                               return g;
                           });
}

FbmOctaves::RidgedSlope FbmOctaves::ridged2DSlope(double x, double z, double sharpness,
                                                  double floor, int head, Memo& memo) const {
    CellGradients g{};
    return ridgedSlopeWith(
        x, z, sharpness, floor, head,
        [&g, &memo](std::size_t i, const Octave& o,
                    const detail::CellPosition& c) -> const CellGradients& {
            if (i < memo.cells.size()) {
                return detail::gradientsThrough(c, o.seed, o.lattice, memo.cells[i]);
            }
            g = detail::gradientsOf(c.iu, c.iv, o.seed, o.lattice);
            return g;
        });
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
            !(la.world == lb.world) || la.reciprocal != lb.reciprocal ||
            !sameBits(la.inverseC, lb.inverseC)) {
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

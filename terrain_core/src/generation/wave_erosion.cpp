#include "wave_erosion.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace terrain::wave_erosion {

namespace {

// The golden angle, in turns: (3 - sqrt(5)) / 2.
constexpr double kGoldenTurns = 0x1.8722191a02d60p-2;
constexpr double kTwoPi = 0x1.921fb54442d18p+2;
// How much the sum already added shifts the phase of the next waves (Big Globe).
constexpr double kBranching = 8.0;
// Waves computed together from the same state.
constexpr std::size_t kGroup = 8;

// [0, 1) from the high 53 bits of a hash.
double unit(std::uint64_t hash) {
    return static_cast<double>(hash >> 11) * 0x1.0p-53;
}

// sin(2 pi a) for a in [-0.25, 0.25]: Taylor to the 9th power, within 4e-6.
double sinQuarter(double a) {
    const double x = kTwoPi * a;
    const double x2 = x * x;
    return x * (1.0 + x2 * (-1.0 / 6.0 +
                            x2 * (1.0 / 120.0 + x2 * (-1.0 / 5040.0 + x2 * (1.0 / 362880.0)))));
}

// sin(2 pi t) and cos(2 pi t), without a branch.
struct SinCos {
    double sin;
    double cos;
};

SinCos sinCosTurns(double t) {
    const double f = t - std::floor(t + 0.5); // [-0.5, 0.5)
    const double m = std::abs(f);
    const double s = sinQuarter(0.25 - std::abs(0.25 - m)); // sin(2 pi |f|)
    return {f < 0.0 ? -s : s, sinQuarter(0.25 - m)};
}

} // namespace

Waves makeWaves(std::uint64_t seed, const WorldSize& world, double maxWavelength,
                double minWavelength, int perOctave) {
    if (!(minWavelength > 0.0) || !(maxWavelength >= minWavelength) || perOctave < 1) {
        throw std::invalid_argument("wave_erosion: invalid wavelengths");
    }
    Waves w;
    const double turn0 = unit(noise::deriveSeed(seed, 0));
    for (int i = 0;; ++i) {
        const double wavelength = maxWavelength * dmath::exp2(-static_cast<double>(i) / perOctave);
        if (wavelength < minWavelength) {
            break;
        }
        const double turn = turn0 + kGoldenTurns * i;
        // The nearest wave of the torus: whole numbers of wavelengths along each side.
        const double m = std::round(world.x * dmath::cosTurns(turn) / wavelength);
        const double n = std::round(world.z * dmath::sinTurns(turn) / wavelength);
        Wave v;
        v.kx = m / world.x;
        v.kz = n / world.z;
        const double k = dmath::hypot(v.kx, v.kz);
        if (!(k > 0.0)) {
            continue;
        }
        v.ux = v.kx / k;
        v.uz = v.kz / k;
        v.phase = unit(noise::deriveSeed(seed, static_cast<std::uint64_t>(i) + 1));
        v.share = 1.0 / (k * maxWavelength);
        w.waves.push_back(v);
    }
    return w;
}

double erode(const Waves& waves, double x, double z, double dx, double dz) {
    const double scale = 0.5 / std::sqrt(dx * dx + dz * dz + 1.0);
    double gx = dx * scale;
    double gz = dz * scale;
    double sum = 0.0;
    const std::size_t count = waves.waves.size();
    // The waves of a group of kGroup see the sum and the gradient before the group: their
    // chains of dependencies run side by side.
    for (std::size_t g = 0; g < count; g += kGroup) {
        const double rcp = 0.5 / std::sqrt(gx * gx + gz * gz + 1.0);
        const double push = -kBranching * sum * rcp / kTwoPi;
        double added = 0.0;
        double addX = 0.0;
        double addZ = 0.0;
        const std::size_t end = std::min(count, g + kGroup);
        for (std::size_t i = g; i < end; ++i) {
            const Wave& w = waves.waves[i];
            const double t = w.kx * x + w.kz * z + w.phase + push * (w.ux * gx + w.uz * gz);
            const double across = (w.uz * gx - w.ux * gz) * rcp;
            const double weight = across * across;
            const SinCos sc = sinCosTurns(t);
            added += sc.sin * weight * w.share;
            const double derivative = sc.cos * weight;
            addX += derivative * w.ux;
            addZ += derivative * w.uz;
        }
        sum += added;
        gx += addX;
        gz += addZ;
    }
    return sum;
}

} // namespace terrain::wave_erosion

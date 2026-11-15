#include "tableland_front.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>

namespace terrain::tableland_front {

float coastFade(double coast, double wobble) {
    const double read = coast - kCoastWobble * (0.5 + 0.5 * wobble);
    return static_cast<float>(noise::smoothstep(static_cast<float>(kCoastNear),
                                                static_cast<float>(kCoastFar),
                                                static_cast<float>(read)));
}

float share(float potential, float lift, const noise::FbmOctaves& edge, double x, double z,
            float coast) {
    if (!(potential + lift + kEdge > kShareLow)) {
        return 0.0f;
    }
    return coast * noise::smoothstep(kShareLow, kShareHigh,
                                     potential + lift + kEdge * edge.fbm2D(x, z));
}

namespace {

// A swell of waves of wavelength meters (whole numbers of them on the world), their phase twisted by
// a noise of twistWavelength.
Swell swellOf(std::uint64_t seed, const WorldSize& world, double wavelength,
              double twistWavelength) {
    Swell out{noise::FbmOctaves(seed,
                                {.octaves = 2,
                                 .frequency = 1.0 / twistWavelength,
                                 .lattices = noise::OctaveLattices::Unrotated},
                                world),
              {},
              {}};
    for (std::size_t k = 0; k < 3; ++k) {
        const dmath::SineCosine heading = dmath::sinCosTurns(static_cast<double>(k) / 6.0);
        out.kx[k] = std::round(world.x * heading.cosine / wavelength) / world.x;
        out.kz[k] = std::round(world.z * heading.sine / wavelength) / world.z;
    }
    return out;
}

// The sum of the waves of swell at (x, z), their phase twisted by up to twist of a turn.
double wavesAt(const Swell& swell, double x, double z, double twistMost) {
    const double twist = twistMost * static_cast<double>(swell.twist.fbm2D(x, z));
    double sum = 0.0;
    for (std::size_t k = 0; k < 3; ++k) {
        sum += dmath::sinTurns(swell.kx[k] * x + swell.kz[k] * z + twist +
                               static_cast<double>(k) / 3.0);
    }
    return sum;
}

} // namespace

Swell makeSwell(std::uint64_t seed, const WorldSize& world) {
    return swellOf(seed, world, kShiftWavelength, kTwistWavelength);
}

double shiftAt(const Swell& swell, double x, double z) {
    return kShift * wavesAt(swell, x, z, kTwist);
}

Swell makeBendSwell(std::uint64_t seed, const WorldSize& world) {
    return swellOf(seed, world, kBendWavelength, kBendTwistWavelength);
}

double bendAt(const Swell& swell, double x, double z) {
    return kBendShift * wavesAt(swell, x, z, kBendTwist);
}

double liftOf(double shift, double east, double west, double north, double south) {
    const double gradient = dmath::hypot(east - west, north - south) / (2.0 * kStep);
    return std::clamp(shift * gradient, -kLiftMost, kLiftMost);
}

double mostLifted(double most, double difference, double cellSize) {
    // The gradient of the bilinear potential is the difference of two neighboring cells over their
    // spacing at most, along each axis.
    const double gradient = dmath::hypot(difference, difference) / cellSize;
    return most + std::min(kLiftMost, 3.0 * kShift * gradient);
}

} // namespace terrain::tableland_front

#include "tableland_forms.hpp"

#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace terrain::tableland_forms {

namespace {

// The high surface: the land from kLandLow to kLandHigh meters, out of the belts (kBeltLow to
// kBeltHigh) and of the rifts (to kRiftHigh), of an intensity below kReliefLow to kReliefHigh.
constexpr float kLandLow = 2200.0f;
constexpr float kLandHigh = 3000.0f;
constexpr float kBeltLow = 0.3f;
constexpr float kBeltHigh = 0.5f;
constexpr float kRiftHigh = 0.1f;
constexpr float kReliefLow = 0.1f;
constexpr float kReliefHigh = 0.16f;

// The inner ranges: the zero lines of a noise of kLineWavelength, warped by up to kWarp meters
// (a noise of kWarpWavelength), a range within kLineWidth of them (in the units of the noise), at
// most kRangeRelief on its axis, kRangeLow of it over most of its width (its strength risen past
// kRangeRise); the stretches where the mask (a noise of kMaskWavelength) passes kMaskLow to
// kMaskHigh.
constexpr double kLineWavelength = 260'000.0;
constexpr double kWarpWavelength = 600'000.0;
constexpr double kWarp = 220'000.0;
constexpr float kLineWidth = 0.115f;
constexpr float kRangeRelief = 0.35f;
constexpr float kRangeLow = 0.6f;
constexpr float kRangeRise = 0.4f;
constexpr double kMaskWavelength = 150'000.0;
constexpr float kMaskLow = -0.1f;
constexpr float kMaskHigh = 0.15f;
// The relief of the surfaces: swells and basins of up to kSwell meters (a noise of
// kSwellWavelength), hills of up to kHill meters (a noise of kHillWavelength).
constexpr double kSwellWavelength = 70'000.0;
constexpr float kSwell = 220.0f;
constexpr double kHillWavelength = 9'000.0;
constexpr float kHill = 45.0f;

noise::FbmOctaves octaves(std::uint64_t seed, std::uint64_t stream, double wavelength,
                          const WorldSize& world) {
    return noise::FbmOctaves(noise::deriveSeed(seed, stream),
                             {.octaves = 2,
                              .frequency = 1.0 / wavelength,
                              .lattices = noise::OctaveLattices::Unrotated},
                             world);
}

} // namespace

Noises makeNoises(std::uint64_t seed, const WorldSize& world) {
    return {octaves(seed, 0, kLineWavelength, world),  octaves(seed, 1, kWarpWavelength, world),
            octaves(seed, 2, kWarpWavelength, world),  octaves(seed, 3, kMaskWavelength, world),
            octaves(seed, 4, kSwellWavelength, world), octaves(seed, 5, kHillWavelength, world),
            world};
}

float surfaceShare(const GeographySample& g) {
    if (!(g.land > kLandLow) || !(g.coast > 0.0)) {
        return 0.0f;
    }
    return noise::smoothstep(kLandLow, kLandHigh, g.land) *
           (1.0f - noise::smoothstep(kBeltLow, kBeltHigh, g.belt)) *
           (1.0f - noise::smoothstep(0.0f, kRiftHigh, g.rift)) *
           (1.0f - noise::smoothstep(kReliefLow, kReliefHigh, g.relief));
}

float innerRanges(const Noises& n, double x, double z, float surface) {
    if (!(surface > 0.0f)) {
        return 0.0f;
    }
    const float mask = noise::smoothstep(kMaskLow, kMaskHigh, n.rangeMask.fbm2D(x, z));
    if (!(mask > 0.0f)) {
        return 0.0f;
    }
    const std::array<float, 2> warp = n.rangeWarpX.fbm2DPair(n.rangeWarpZ, x, z);
    const WorldSize& world = n.world;
    const float line =
        n.rangeLines.fbm2D(noise::wrapCanonical(x + kWarp * warp[0], world.x),
                           noise::wrapCanonical(z + kWarp * warp[1], world.z));
    const float axis = 1.0f - noise::smoothstep(0.0f, kLineWidth, std::abs(line));
    // From kRangeLow of kRangeRelief over most of its width up to all of it on its axis, rising
    // quickly from its foot.
    const float strength = mask * axis * (2.0f - axis);
    return kRangeRelief * surface * noise::smoothstep(0.0f, kRangeRise, strength) *
           (kRangeLow + (1.0f - kRangeLow) * strength);
}

float surfaceRelief(const Noises& n, double x, double z, float surface) {
    if (!(surface > 0.0f)) {
        return 0.0f;
    }
    return surface * (kSwell * n.swells.fbm2D(x, z) + kHill * n.hills.fbm2D(x, z));
}

} // namespace terrain::tableland_forms

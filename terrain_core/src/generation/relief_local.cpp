#include "relief_local.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <cmath>

namespace terrain::relief_local {

namespace {

// Independent streams of the seed of the local relief. New streams go at the end.
enum Stream : std::uint64_t {
    kHills,
    kVallons,
    kButtes,
    kRegions,
};

constexpr double kHillsWavelength = 6'000.0;
constexpr double kHillsGain = 0.55;
constexpr double kVallonWavelength = 14'000.0;
constexpr double kButteWavelength = 3'500.0;
constexpr double kRegionWavelength = 200'000.0;

// The vallons deepen where the noise of the regions rises past kVallonLands, the buttes rise
// where it falls past kButteLands (both fade in over kLandsFade of it).
constexpr float kVallonLands = -0.25f;
constexpr float kButteLands = 0.0f;
constexpr float kLandsFade = 0.35f;

// A vallon follows the zero lines of its noise, kVallonWidth of it on each side: about 1 km
// wide, its walls concave at their foot.
constexpr float kVallonWidth = 0.12f;

// The buttes rise where their noise goes from kButteFoot to kButteTop: a few percent of the
// ground, their sides about 100 m wide.
constexpr float kButteFoot = 0.38f;
constexpr float kButteTop = 0.46f;

float fbm(const noise::FbmOctaves& layer, double x, double z, noise::FbmOctaves::Memo* memo) {
    return memo != nullptr ? layer.fbm2D(x, z, *memo) : layer.fbm2D(x, z);
}

} // namespace

Noises makeNoises(std::uint64_t seed, const WorldSize& world) {
    const auto layer = [seed, &world](Stream stream, int octaves, double wavelength,
                                      double gain) {
        return noise::FbmOctaves(noise::deriveSeed(seed, stream),
                                 {.octaves = octaves,
                                  .frequency = 1.0 / wavelength,
                                  .gain = gain,
                                  .lattices = noise::OctaveLattices::RotatedFast},
                                 world);
    };
    return {layer(kHills, 4, kHillsWavelength, kHillsGain),
            layer(kVallons, 3, kVallonWavelength, 0.5), layer(kButtes, 3, kButteWavelength, 0.5),
            layer(kRegions, 2, kRegionWavelength, 0.5)};
}

float rolling(const Noises& noises, double x, double z, float amplitude, float butteHeight,
              Memos* memos) {
    const float hills = fbm(noises.hills, x, z, memos != nullptr ? &memos->hills : nullptr);
    const float region = fbm(noises.regions, x, z, memos != nullptr ? &memos->regions : nullptr);
    const float vallons = noise::smoothstep(kVallonLands, kVallonLands + kLandsFade, region);
    float relief = amplitude * hills;
    if (vallons > 0.0f) {
        const float across =
            fbm(noises.vallons, x, z, memos != nullptr ? &memos->vallons : nullptr) /
            kVallonWidth;
        const float inside = 1.0f - across * across;
        relief -= amplitude * kVallonDepth * vallons * (inside > 0.0f ? inside * inside : 0.0f);
    }
    const float buttes =
        butteHeight * noise::smoothstep(kButteLands, kButteLands + kLandsFade, -region);
    if (!(buttes > 0.0f)) {
        return relief;
    }
    return relief + buttes * noise::smoothstep(kButteFoot, kButteTop,
                                               fbm(noises.buttes, x, z,
                                                   memos != nullptr ? &memos->buttes : nullptr));
}

float boundBelow(float local, float room) {
    if (local >= 0.0f) {
        return local;
    }
    if (!(room > 0.0f)) {
        return 0.0f;
    }
    const double r = static_cast<double>(room);
    return static_cast<float>(-r * dmath::tanh(-static_cast<double>(local) / r));
}

} // namespace terrain::relief_local

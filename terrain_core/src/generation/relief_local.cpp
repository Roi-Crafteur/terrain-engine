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
    kBroadHills,
};

// The noises of a style of relief: the lively plains, or the relief of step 2.
struct Style {
    double hillsWavelength; // of the plains (lively) or everywhere (step 2)
    double hillsGain;
    int vallonOctaves;
    double vallonWavelength;
    int butteOctaves;
    double butteWavelength;
};

constexpr Style kLively = {800.0, 0.7, 2, 5'000.0, 2, 1'200.0};
constexpr Style kStepTwo = {6'000.0, 0.55, 3, 14'000.0, 3, 3'500.0};
// The hills of the rough country of the lively plains.
constexpr double kBroadWavelength = 4'500.0;

// A vallon follows the zero lines of its noise, kVallonWidth of it on each side: about 1 km
// wide, its walls concave at their foot.
constexpr float kVallonWidth = 0.09f;

// The buttes rise where their noise goes from their foot to kButteRise above it: their sides
// about 100 m wide. Lively plains: a foot from kButteFoot in the lands of buttes to kButteFoot +
// kButteShift in those of vallons (fewer there, never none).
constexpr float kButteFoot = 0.24f;
constexpr float kButteShift = 0.12f;
constexpr float kButteRise = 0.07f;
// The sides take kButteSides of the height of a butte, its dome the rest up to kButteDome above
// its foot.
constexpr float kButteSides = 0.6f;
constexpr float kButteDome = 0.3f;

// Lively plains: the lands of vallons and of buttes pass from one to the other as the regions go
// from -kLandsSpan to kLandsSpan; between them, where |regions| is below kTerraceCore (fading out
// at kTerraceLands), soft terraces of kTerraceStep meters: flat treads, steeper risers.
constexpr float kLandsSpan = 0.4f;
constexpr float kTerraceCore = 0.04f;
constexpr float kTerraceLands = 0.12f;
constexpr double kTerraceStep = 10.0;

float fbm(const noise::FbmOctaves& layer, double x, double z, noise::FbmOctaves::Memo* memo) {
    return memo != nullptr ? layer.fbm2D(x, z, *memo) : layer.fbm2D(x, z);
}

// Soft terraces: flat treads over 60 % of each step, then the riser.
float terrace(float h) {
    const double steps = static_cast<double>(h) / kTerraceStep;
    const double k = std::floor(steps);
    const double f = steps - k;
    const double t = f <= 0.6 ? 0.0 : (f - 0.6) / 0.4;
    return static_cast<float>(kTerraceStep * (k + t * t * (3.0 - 2.0 * t)));
}

// The share of the vallons, the foot of the buttes and the share of the terraces at a point.
struct Lands {
    float vallons;
    float butteFoot;
    float buttes;
    float terraces;
};

Lands landsOf(bool lively, const Shape& shape) {
    if (!lively) {
        // Step 2: lands of vallons only above -0.25, of buttes only below 0.
        return {shape.plain * noise::smoothstep(-0.25f, 0.10f, shape.region), 0.38f,
                noise::smoothstep(0.0f, 0.35f, -shape.region), 0.0f};
    }
    const float vallonLands = noise::smoothstep(-kLandsSpan, kLandsSpan, shape.region);
    return {shape.plain * (0.5f + 0.5f * vallonLands), kButteFoot + kButteShift * vallonLands,
            1.0f,
            shape.plain *
                (1.0f - noise::smoothstep(kTerraceCore, kTerraceLands, std::abs(shape.region)))};
}

} // namespace

Noises makeNoises(std::uint64_t seed, const WorldSize& world, bool lively) {
    const Style& style = lively ? kLively : kStepTwo;
    const auto layer = [seed, &world](Stream stream, int octaves, double wavelength,
                                      double gain) {
        return noise::FbmOctaves(noise::deriveSeed(seed, stream),
                                 {.octaves = octaves,
                                  .frequency = 1.0 / wavelength,
                                  .gain = gain,
                                  .lattices = noise::OctaveLattices::RotatedFast},
                                 world);
    };
    return {layer(kHills, lively ? 3 : 4, style.hillsWavelength, style.hillsGain),
            lively ? layer(kBroadHills, 3, kBroadWavelength, style.hillsGain)
                   : noise::FbmOctaves(),
            layer(kVallons, style.vallonOctaves, style.vallonWavelength, 0.5),
            layer(kButtes, style.butteOctaves, style.butteWavelength, 0.5), lively};
}

// The hills, from those of the plains to those of the rough country (lively plains), only the
// noises needed.
float hillsAt(const Noises& noises, double x, double z, float rough, Memos* memos) {
    const float broad = noises.lively ? rough : 0.0f;
    const float plain =
        broad < 1.0f ? fbm(noises.hills, x, z, memos != nullptr ? &memos->hills : nullptr) : 0.0f;
    if (!(broad > 0.0f)) {
        return plain;
    }
    const float wide =
        fbm(noises.broadHills, x, z, memos != nullptr ? &memos->broadHills : nullptr);
    return plain + broad * (wide - plain);
}

float rolling(const Noises& noises, double x, double z, const Shape& shape, Memos* memos) {
    const Lands lands = landsOf(noises.lively, shape);
    float relief = shape.amplitude * hillsAt(noises, x, z, shape.rough, memos);
    if (lands.terraces > 0.0f) {
        relief += lands.terraces * (terrace(relief) - relief);
    }
    if (lands.vallons > 0.0f) {
        const float across =
            fbm(noises.vallons, x, z, memos != nullptr ? &memos->vallons : nullptr) /
            kVallonWidth;
        const float inside = 1.0f - across * across;
        relief -= shape.amplitude * kVallonDepth * lands.vallons *
                  (inside > 0.0f ? inside * inside : 0.0f);
    }
    const float buttes = shape.buttes * lands.buttes;
    if (!(buttes > 0.0f)) {
        return relief;
    }
    const float n = fbm(noises.buttes, x, z, memos != nullptr ? &memos->buttes : nullptr);
    // Steep sides, then a domed top.
    return relief +
           buttes * (kButteSides * noise::smoothstep(lands.butteFoot, lands.butteFoot + kButteRise, n) +
                     (1.0f - kButteSides) *
                         noise::smoothstep(lands.butteFoot, lands.butteFoot + kButteDome, n));
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

#include "terrain/generation/heightfield.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/noise_octaves.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>

namespace terrain {

namespace {

// Continental curve: altitude as a function of continentalness - coastContinentalness.
// Strictly increasing, 0 at the coast, from the abyssal plain to inland highlands.
struct CurvePoint {
    double offset;
    double height;
};

constexpr std::array<CurvePoint, 8> kContinentCurve = {{
    {-0.60, kMinBaseHeight}, // abyssal plain
    {-0.30, -1800.0},        // foot of the continental slope
    {-0.12, -200.0},         // edge of the continental shelf
    {-0.04, -40.0},          // shallow shelf
    {0.00, 0.0},             // coast
    {0.10, 60.0},            // coastal plains
    {0.30, 250.0},           // inland plains
    {0.60, kMaxBaseHeight},  // highlands
}};
constexpr std::size_t kCurveSize = kContinentCurve.size();

// Monotone cubic Hermite tangents (PCHIP, weighted harmonic mean of the secants): the
// curve is C1 and never overshoots its control points.
constexpr std::array<double, kCurveSize> curveTangents() {
    std::array<double, kCurveSize - 1> secant{};
    for (std::size_t i = 0; i + 1 < kCurveSize; ++i) {
        secant[i] = (kContinentCurve[i + 1].height - kContinentCurve[i].height) /
                    (kContinentCurve[i + 1].offset - kContinentCurve[i].offset);
    }
    std::array<double, kCurveSize> tangent{};
    tangent[0] = secant[0];
    tangent[kCurveSize - 1] = secant[kCurveSize - 2];
    for (std::size_t i = 1; i + 1 < kCurveSize; ++i) {
        const double before = kContinentCurve[i].offset - kContinentCurve[i - 1].offset;
        const double after = kContinentCurve[i + 1].offset - kContinentCurve[i].offset;
        const double w1 = 2.0 * after + before;
        const double w2 = after + 2.0 * before;
        tangent[i] = (w1 + w2) / (w1 / secant[i - 1] + w2 / secant[i]);
    }
    return tangent;
}
constexpr std::array<double, kCurveSize> kCurveTangents = curveTangents();

float continentalBase(double offset) {
    if (offset <= kContinentCurve.front().offset) {
        return kMinBaseHeight;
    }
    if (offset >= kContinentCurve.back().offset) {
        return kMaxBaseHeight;
    }
    std::size_t i = 0;
    while (offset > kContinentCurve[i + 1].offset) {
        ++i;
    }
    const CurvePoint a = kContinentCurve[i];
    const CurvePoint b = kContinentCurve[i + 1];
    const double span = b.offset - a.offset;
    const double t = (offset - a.offset) / span;
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double height = (2.0 * t3 - 3.0 * t2 + 1.0) * a.height +
                          (t3 - 2.0 * t2 + t) * span * kCurveTangents[i] +
                          (-2.0 * t3 + 3.0 * t2) * b.height +
                          (t3 - t2) * span * kCurveTangents[i + 1];
    return static_cast<float>(height);
}

// Mountains fade in between these continentalness offsets above the coast.
constexpr float kMountainCoastStart = 0.02f;
constexpr float kMountainCoastFull = 0.15f;

// Detail fades out within this many detail amplitudes of sea level. Any factor above
// 9 / 8 keeps |detail| < |base|, so the ground is below sea level exactly where the
// continentalness is below the coast.
constexpr float kDetailCoastFade = 4.0f;

// New layers go at the end so that the seeds of the existing ones never change.
enum Layer : std::uint64_t {
    kContinent,
    kMask,
    kWarpX,
    kWarpZ,
    kRidge,
    kDetail,
    kMacroContinent,
    kPlates,
};

} // namespace

// The noises of the layers, with the parameters sampleAt gives them: the same bits as the free
// functions (FbmOctaves). The detail and macro layers are only built when enabled, as they are
// only evaluated then.
struct Heightfield::Layers {
    noise::FbmOctaves continent;
    noise::FbmOctaves macroContinent;
    noise::FbmOctaves mask;
    noise::FbmOctaves warpX;
    noise::FbmOctaves warpZ;
    noise::FbmOctaves ridge;
    noise::FbmOctaves detail;
};

Heightfield::Heightfield(std::uint64_t seed, const HeightfieldParams& params,
                         const WorldSize& world)
    : params_(params), world_(world) {
    validate(world_);
    const auto layer = [seed, &world](Layer stream, int octaves, double wavelength) {
        return noise::FbmOctaves(noise::deriveSeed(seed, stream),
                                 {.octaves = octaves, .frequency = 1.0 / wavelength}, world);
    };
    auto layers = std::make_shared<Layers>();
    layers->continent = layer(kContinent, 4, params_.continentWavelength);
    if (params_.macroContinent > 0.0f) {
        layers->macroContinent = layer(kMacroContinent, 2, params_.macroContinentWavelength);
    }
    layers->mask = layer(kMask, 3, params_.maskWavelength);
    layers->warpX = layer(kWarpX, 3, params_.warpWavelength);
    layers->warpZ = layer(kWarpZ, 3, params_.warpWavelength);
    layers->ridge = layer(kRidge, params_.ridgeOctaves, params_.ridgeWavelength);
    if (params_.detailAmplitude > 0.0f) {
        layers->detail = layer(kDetail, 3, params_.detailWavelength);
    }
    layers_ = std::move(layers);
    if (params_.plates.amplitude > 0.0f) {
        plates_ = std::make_shared<const TectonicPlates>(noise::deriveSeed(seed, kPlates),
                                                         params_.plates, world_);
    }
}

float Heightfield::heightAt(double x, double z) const {
    return sampleAt(x, z).height;
}

HeightSample Heightfield::sampleAt(double worldX, double worldZ) const {
    const double x = noise::wrapCanonical(worldX, world_.x);
    const double z = noise::wrapCanonical(worldZ, world_.z);
    HeightSample s;
    continentalnessAt(x, z, s);
    const float coastOffset = s.continentalness - params_.coastContinentalness;
    s.base = continentalBase(coastOffset);

    const float land = noise::smoothstep(kMountainCoastStart, kMountainCoastFull, coastOffset);
    const float massif =
        noise::smoothstep(params_.maskLow, params_.maskHigh, layers_->mask.fbm2D(x, z));
    // Collision belts are massifs too: the ridged noise draws mountain ranges along them.
    // Without plates the collision is 0 and the mask is unchanged.
    s.mountainMask = land * std::max(massif, s.plateCollision);
    s.mountain = mountainAt(x, z, s.mountainMask);
    if (s.plateCollision > 0.0f) {
        s.mountain *= 1.0f + params_.plates.collisionMountainBoost * s.plateCollision;
    }

    if (params_.detailAmplitude > 0.0f) {
        const float fade = noise::smoothstep(0.0f, kDetailCoastFade * params_.detailAmplitude,
                                             std::abs(s.base));
        s.detail = params_.detailAmplitude * fade * layers_->detail.fbm2D(x, z);
    }
    s.height = s.base + s.mountain + s.detail;
    return s;
}

// Local continentalness, plus the saturated macro term and the plate term when these layers
// are enabled: clamp(local + macroContinent * tanh(macro / macroContinentWidth) +
// plates.amplitude * plate, -1, 1). Without them, the result is exactly the local value.
void Heightfield::continentalnessAt(double x, double z, HeightSample& s) const {
    const float local = layers_->continent.fbm2D(x, z);
    s.continentalness = local;
    if (params_.macroContinent <= 0.0f && !plates_) {
        return;
    }
    float term = 0.0f;
    if (params_.macroContinent > 0.0f) {
        s.macroContinentalness = layers_->macroContinent.fbm2D(x, z);
        term =
            params_.macroContinent *
            static_cast<float>(dmath::tanh(s.macroContinentalness / params_.macroContinentWidth));
    }
    if (plates_) {
        const PlateSample plate = plates_->sampleAt(x, z);
        s.plateContinentalness = plate.continentalness;
        s.plateCollision = plate.collision;
        term += params_.plates.amplitude * plate.continentalness;
    }
    s.continentalness = std::clamp(local + term, -1.0f, 1.0f);
}

// The ridged noise is only evaluated inside the mountain mask, and is scaled by it.
float Heightfield::mountainAt(double x, double z, float mask) const {
    if (mask <= 0.0f) {
        return 0.0f;
    }
    const double warpedX = x + params_.warpAmplitude * layers_->warpX.fbm2D(x, z);
    const double warpedZ = z + params_.warpAmplitude * layers_->warpZ.fbm2D(x, z);
    return mask * params_.mountainHeight *
           layers_->ridge.ridged2D(warpedX, warpedZ, params_.ridgeSharpness);
}

} // namespace terrain

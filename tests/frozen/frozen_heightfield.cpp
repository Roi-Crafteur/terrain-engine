// FROZEN REFERENCE, do not edit: terrain_core/src/generation/heightfield.cpp as of commit
// 052078b (before the noise performance work), copied verbatim into namespace frozen over the
// frozen noise and plates, then CaveNoiseLattice::nodeValue of cave_noise.cpp.
#include "frozen_heightfield.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace frozen {

using terrain::validate;

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

Heightfield::Heightfield(std::uint64_t seed, const HeightfieldParams& params,
                         const WorldSize& world)
    : params_(params), world_(world), continentSeed_(noise::deriveSeed(seed, kContinent)),
      maskSeed_(noise::deriveSeed(seed, kMask)), warpXSeed_(noise::deriveSeed(seed, kWarpX)),
      warpZSeed_(noise::deriveSeed(seed, kWarpZ)), ridgeSeed_(noise::deriveSeed(seed, kRidge)),
      detailSeed_(noise::deriveSeed(seed, kDetail)),
      macroContinentSeed_(noise::deriveSeed(seed, kMacroContinent)) {
    validate(world_);
    if (params_.plates.amplitude > 0.0f) {
        plates_ = std::make_shared<const TectonicPlates>(noise::deriveSeed(seed, kPlates),
                                                         params_.plates, world_);
    }
}

float Heightfield::heightAt(double x, double z) const {
    return sampleAt(x, z).height;
}

HeightSample Heightfield::sampleAt(double worldX, double worldZ) const {
    const double x = wrapCoordinate(worldX, world_.x);
    const double z = wrapCoordinate(worldZ, world_.z);
    HeightSample s;
    continentalnessAt(x, z, s);
    const float coastOffset = s.continentalness - params_.coastContinentalness;
    s.base = continentalBase(coastOffset);

    const noise::FbmParams mask{.octaves = 3, .frequency = 1.0 / params_.maskWavelength};
    const float land = noise::smoothstep(kMountainCoastStart, kMountainCoastFull, coastOffset);
    const float massif = noise::smoothstep(params_.maskLow, params_.maskHigh,
                                           noise::fbm2D(x, z, maskSeed_, mask, world_));
    // Collision belts are massifs too: the ridged noise draws mountain ranges along them.
    // Without plates the collision is 0 and the mask is unchanged.
    s.mountainMask = land * std::max(massif, s.plateCollision);
    s.mountain = mountainAt(x, z, s.mountainMask);
    if (s.plateCollision > 0.0f) {
        s.mountain *= 1.0f + params_.plates.collisionMountainBoost * s.plateCollision;
    }

    if (params_.detailAmplitude > 0.0f) {
        const noise::FbmParams detail{.octaves = 3, .frequency = 1.0 / params_.detailWavelength};
        const float fade = noise::smoothstep(0.0f, kDetailCoastFade * params_.detailAmplitude,
                                             std::abs(s.base));
        s.detail = params_.detailAmplitude * fade * noise::fbm2D(x, z, detailSeed_, detail, world_);
    }
    s.height = s.base + s.mountain + s.detail;
    return s;
}

// Local continentalness, plus the saturated macro term and the plate term when these layers
// are enabled: clamp(local + macroContinent * tanh(macro / macroContinentWidth) +
// plates.amplitude * plate, -1, 1). Without them, the result is exactly the local value.
void Heightfield::continentalnessAt(double x, double z, HeightSample& s) const {
    const noise::FbmParams continent{.octaves = 4, .frequency = 1.0 / params_.continentWavelength};
    const float local = noise::fbm2D(x, z, continentSeed_, continent, world_);
    s.continentalness = local;
    if (params_.macroContinent <= 0.0f && !plates_) {
        return;
    }
    float term = 0.0f;
    if (params_.macroContinent > 0.0f) {
        const noise::FbmParams macro{.octaves = 2,
                                     .frequency = 1.0 / params_.macroContinentWavelength};
        s.macroContinentalness = noise::fbm2D(x, z, macroContinentSeed_, macro, world_);
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
    const noise::FbmParams warp{.octaves = 3, .frequency = 1.0 / params_.warpWavelength};
    const double warpedX = x + params_.warpAmplitude * noise::fbm2D(x, z, warpXSeed_, warp, world_);
    const double warpedZ = z + params_.warpAmplitude * noise::fbm2D(x, z, warpZSeed_, warp, world_);
    const noise::FbmParams ridge{.octaves = params_.ridgeOctaves,
                                 .frequency = 1.0 / params_.ridgeWavelength};
    return mask * params_.mountainHeight *
           noise::ridged2D(warpedX, warpedZ, ridgeSeed_, ridge, world_, params_.ridgeSharpness);
}

// CaveNoiseLattice::nodeValue: layers_[noise], nodesX_, nodesZ_ and world_ of the lattice.
float caveNodeValue(const terrain::CaveNoiseLattice& lattice, terrain::CaveNoise noise,
                    std::int64_t i, std::int64_t j, std::int64_t k, const WorldSize& world) {
    using terrain::kCaveNodeMeters;
    const WorldSize& world_ = world;
    const terrain::PeriodicCells& nodesX_ = lattice.nodesX();
    const terrain::PeriodicCells& nodesZ_ = lattice.nodesZ();
    const terrain::CaveNoiseLayer& layer = lattice.layer(noise);
    const double x = static_cast<double>(nodesX_.wrap(i)) * nodesX_.size;
    const double y = static_cast<double>(j) * kCaveNodeMeters * layer.verticalScale;
    const double z = static_cast<double>(nodesZ_.wrap(k)) * nodesZ_.size;
    const noise::FbmParams fbm{layer.octaves, layer.frequency, 2.0, 0.5};
    return noise::fbm3D(x, y, z, layer.seed, fbm, world_);
}

} // namespace frozen

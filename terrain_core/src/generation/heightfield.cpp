#include "terrain/generation/heightfield.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/noise_octaves.hpp"
#include "valleys.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
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
    kGeography,
    kValleys,
};

// The mountains of the geography: the mass shaped by the valleys, kMassBase of its height plus
// kCrag times ridged crags of kCragOctaves from kCragWavelength.
constexpr float kMassBase = 0.7f;
constexpr float kCrag = 0.45f;
constexpr double kCragWavelength = 12'000.0;
constexpr int kCragOctaves = 4;

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
    noise::FbmOctaves geographyDetail; // the detail of the geography: unrotated lattices
    // The warp and ridged noises of the mountains of the geography: fast rotated lattices.
    noise::FbmOctaves geographyWarpX;
    noise::FbmOctaves geographyWarpZ;
    noise::FbmOctaves geographyRidge;
    valleys::Network valleys;
    // Mask edges finite and at least a normal float apart: smoothstep then divides by a nonzero
    // finite number, and the massif is finite, in [+0, 1] and never -0 for every value of the
    // mask noise (heightfield_shortcuts_test), so the noise can be skipped where the land
    // factor is 0.
    bool maskSkippable = false;
    bool twinWarps = false;          // warpX and warpZ are twins (FbmOctaves::twinOf)
    bool twinGeographyWarps = false; // so are geographyWarpX and geographyWarpZ
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
        layers->geographyDetail = noise::FbmOctaves(
            noise::deriveSeed(seed, kDetail),
            {.octaves = 3,
             .frequency = 1.0 / params_.detailWavelength,
             .lattices = noise::OctaveLattices::Unrotated},
            world);
    }
    layers->twinWarps = layers->warpX.twinOf(layers->warpZ);
    if (params_.geography.enabled) {
        const auto fast = [seed, &world](Layer stream, int octaves, double wavelength) {
            return noise::FbmOctaves(noise::deriveSeed(seed, stream),
                                     {.octaves = octaves,
                                      .frequency = 1.0 / wavelength,
                                      .lattices = noise::OctaveLattices::RotatedFast},
                                     world);
        };
        layers->geographyWarpX = fast(kWarpX, 3, params_.warpWavelength);
        layers->geographyWarpZ = fast(kWarpZ, 3, params_.warpWavelength);
        layers->geographyRidge = fast(kRidge, kCragOctaves, kCragWavelength);
        layers->valleys = valleys::makeNetwork(noise::deriveSeed(seed, kValleys), world);
        layers->twinGeographyWarps = layers->geographyWarpX.twinOf(layers->geographyWarpZ);
    }
    layers->maskSkippable = std::isfinite(params_.maskLow) && std::isfinite(params_.maskHigh) &&
                            params_.maskHigh - params_.maskLow >=
                                std::numeric_limits<float>::min();
    layers_ = std::move(layers);
    if (params_.geography.enabled) {
        geography_ =
            Geography::shared(noise::deriveSeed(seed, kGeography), params_.geography, world_);
        return;
    }
    if (params_.plates.amplitude > 0.0f) {
        plates_ = std::make_shared<const TectonicPlates>(noise::deriveSeed(seed, kPlates),
                                                         params_.plates, world_);
    }
}

// The memos of the noises of the layers and of the plates during a batch call.
struct Heightfield::Memos {
    noise::FbmOctaves::Memo continent;
    noise::FbmOctaves::Memo macroContinent;
    noise::FbmOctaves::Memo mask;
    noise::FbmOctaves::Memo warpX;
    noise::FbmOctaves::Memo warpZ;
    noise::FbmOctaves::Memo ridge;
    noise::FbmOctaves::Memo detail;
    TectonicPlates::Memos plates;
};

namespace {

// fbm2D of a layer, through its memo during a batch call.
template <class Memos>
float fbm(const noise::FbmOctaves& layer, double x, double z, Memos* memos,
          noise::FbmOctaves::Memo Memos::*memo) {
    return memos ? layer.fbm2D(x, z, memos->*memo) : layer.fbm2D(x, z);
}

void requireOneSize(std::size_t xs, std::size_t zs, std::size_t out) {
    if (xs != zs || xs != out) {
        throw std::invalid_argument("Heightfield: the points and the results differ in size");
    }
}

} // namespace

float Heightfield::heightAt(double x, double z) const {
    return sampleWith(x, z, nullptr).height;
}

HeightSample Heightfield::sampleAt(double x, double z) const {
    return sampleWith(x, z, nullptr);
}

void Heightfield::samplesAt(std::span<const double> xs, std::span<const double> zs,
                            std::span<HeightSample> out) const {
    requireOneSize(xs.size(), zs.size(), out.size());
    Memos memos;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        out[i] = sampleWith(xs[i], zs[i], &memos);
    }
}

void Heightfield::heightsAt(std::span<const double> xs, std::span<const double> zs,
                            std::span<float> out) const {
    requireOneSize(xs.size(), zs.size(), out.size());
    Memos memos;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        out[i] = sampleWith(xs[i], zs[i], &memos).height;
    }
}

HeightSample Heightfield::sampleWith(double worldX, double worldZ, Memos* memos) const {
    const double x = noise::wrapCanonical(worldX, world_.x);
    const double z = noise::wrapCanonical(worldZ, world_.z);
    if (geography_) {
        return geographySample(x, z, memos);
    }
    HeightSample s;
    continentalnessAt(x, z, s, memos);
    const float coastOffset = s.continentalness - params_.coastContinentalness;
    s.base = continentalBase(coastOffset);

    const float land = noise::smoothstep(kMountainCoastStart, kMountainCoastFull, coastOffset);
    // On the sea and the shore the land factor is 0, and the mask 0 * max(massif, collision)
    // whatever the massif: both are finite, at least +0 and never -0, so the mask is a zero of
    // the sign of land either way. The mask noise is skipped there.
    float massif = 0.0f;
    if (land != 0.0f || !layers_->maskSkippable) {
        massif = noise::smoothstep(params_.maskLow, params_.maskHigh,
                                   fbm(layers_->mask, x, z, memos, &Memos::mask));
    }
    // Collision belts are massifs too: the ridged noise draws mountain ranges along them.
    // Without plates the collision is 0 and the mask is unchanged.
    s.mountainMask = land * std::max(massif, s.plateCollision);
    s.mountain = mountainAt(x, z, s.mountainMask, memos);
    if (s.plateCollision > 0.0f) {
        s.mountain *= 1.0f + params_.plates.collisionMountainBoost * s.plateCollision;
    }

    s.detail = detailAt(x, z, s.base, memos);
    s.height = s.base + s.mountain + s.detail;
    return s;
}

// The detail fades out within kDetailCoastFade amplitudes of sea level, so that it never moves
// the coast.
float Heightfield::detailAt(double x, double z, float base, Memos* memos) const {
    if (!(params_.detailAmplitude > 0.0f)) {
        return 0.0f;
    }
    const float fade =
        noise::smoothstep(0.0f, kDetailCoastFade * params_.detailAmplitude, std::abs(base));
    const noise::FbmOctaves& layer = geography_ ? layers_->geographyDetail : layers_->detail;
    return params_.detailAmplitude * fade * fbm(layer, x, z, memos, &Memos::detail);
}

// Local continentalness, plus the saturated macro term and the plate term when these layers
// are enabled: clamp(local + macroContinent * tanh(macro / macroContinentWidth) +
// plates.amplitude * plate, -1, 1). Without them, the result is exactly the local value.
void Heightfield::continentalnessAt(double x, double z, HeightSample& s, Memos* memos) const {
    const float local = fbm(layers_->continent, x, z, memos, &Memos::continent);
    s.continentalness = local;
    if (params_.macroContinent <= 0.0f && !plates_) {
        return;
    }
    float term = 0.0f;
    if (params_.macroContinent > 0.0f) {
        s.macroContinentalness =
            fbm(layers_->macroContinent, x, z, memos, &Memos::macroContinent);
        term =
            params_.macroContinent *
            static_cast<float>(dmath::tanh(s.macroContinentalness / params_.macroContinentWidth));
    }
    if (plates_) {
        const PlateSample plate =
            memos ? plates_->sampleAt(x, z, memos->plates) : plates_->sampleAt(x, z);
        s.plateContinentalness = plate.continentalness;
        s.plateCollision = plate.collision;
        term += params_.plates.amplitude * plate.continentalness;
    }
    s.continentalness = std::clamp(local + term, -1.0f, 1.0f);
}

// The ridged noise is only evaluated inside the mountain mask, and is scaled by it.
float Heightfield::mountainAt(double x, double z, float mask, Memos* memos) const {
    if (mask <= 0.0f) {
        return 0.0f;
    }
    std::array<float, 2> warp{};
    return mask * params_.mountainHeight * ridgeAt(x, z, memos, warp);
}

// The geography has its own noises.
float Heightfield::ridgeAt(double x, double z, Memos* memos, std::array<float, 2>& warp) const {
    const bool geography = geography_ != nullptr;
    const noise::FbmOctaves& warpX = geography ? layers_->geographyWarpX : layers_->warpX;
    const noise::FbmOctaves& warpZ = geography ? layers_->geographyWarpZ : layers_->warpZ;
    const noise::FbmOctaves& ridged = geography ? layers_->geographyRidge : layers_->ridge;
    if (!(geography ? layers_->twinGeographyWarps : layers_->twinWarps)) {
        warp = {fbm(warpX, x, z, memos, &Memos::warpX), fbm(warpZ, x, z, memos, &Memos::warpZ)};
    } else if (memos) {
        warp = warpX.fbm2DPair(warpZ, x, z, memos->warpX, memos->warpZ);
    } else {
        warp = warpX.fbm2DPair(warpZ, x, z);
    }
    const double warpedX = x + params_.warpAmplitude * warp[0];
    const double warpedZ = z + params_.warpAmplitude * warp[1];
    return memos ? ridged.ridged2D(warpedX, warpedZ, params_.ridgeSharpness, memos->ridge)
                 : ridged.ridged2D(warpedX, warpedZ, params_.ridgeSharpness);
}

// The mountains of the geography: a mass between the valleys of its drainage (valleys, 0 on
// their floors), furrowed by tributaries down the regional elevation, crowned by ridged crags.
float Heightfield::geographyMountainAt(double x, double z, float mask, const GeographySample& g,
                                       float valleys, Memos* memos) const {
    if (mask <= 0.0f || valleys <= 0.0f) {
        return 0.0f;
    }
    const double downX = -static_cast<double>(g.landDX);
    const double downZ = -static_cast<double>(g.landDZ);
    const float mass = valleys * valleys::carve(layers_->valleys, x, z, downX, downZ,
                                                dmath::hypot(downX, downZ));
    std::array<float, 2> warp{};
    const float crags = kMassBase + kCrag * ridgeAt(x, z, memos, warp);
    return mask * params_.geography.mountainHeight * mass * crags;
}

} // namespace terrain

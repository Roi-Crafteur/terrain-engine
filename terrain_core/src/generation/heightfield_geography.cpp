#include "terrain/generation/heightfield.hpp"

#include "heightfield_layers.hpp"
#include "relief_local.hpp"
#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace terrain {

namespace {

// Distance to the coast at which the continentalness reaches 1: as before, the coast at 0 and
// the deep interior beyond 0.6 for the climate.
constexpr double kContinentalScale = 1'500'000.0;

// The mountains (the mask of their massifs) rise with the intensity of the relief from
// kMountainLow to kMountainHigh; their height from kHeightLow to the mountainHeight of the
// geography from kHeightHigh on, worn ones kWorn of it.
constexpr float kMountainLow = 0.12f;
constexpr float kMountainHigh = 0.5f;
constexpr float kHeightLow = 0.1f;
constexpr float kHeightHigh = 0.7f;
constexpr float kWorn = 0.65f;

// The local relief: kPlainHills of amplitude everywhere on land (never flat), up to
// kPlainHills + kHillsRange where the intensity passes kHillsLow to kHillsHigh; buttes
// kButteShare of it high, at most kButteMax, where the intensity is below kButteLow to
// kButteHigh; vallons and terraces fading under the mountains (mask kPlainLow to kPlainHigh).
constexpr float kPlainHills = 18.0f;
constexpr float kHillsRange = 450.0f;
constexpr float kHillsLow = 0.08f;
constexpr float kHillsHigh = 0.7f;
constexpr float kButteShare = 1.6f;
constexpr float kButteMax = 40.0f;
constexpr float kButteLow = 0.1f;
constexpr float kButteHigh = 0.25f;
// The hills keep 1 - kHillsUnderMassifs of their amplitude under a full mask of the massifs.
constexpr float kHillsUnderMassifs = 0.8f;
constexpr float kPlainLow = 0.3f;
constexpr float kPlainHigh = 0.6f;
// The hills of the plains give way to those of the rough country as the intensity passes
// kRoughLow to kRoughHigh (relief_local::Shape::rough).
constexpr float kRoughLow = 0.12f;
constexpr float kRoughHigh = 0.3f;

// The local relief never takes more than this share of the ground above the sea level away
// from it: the land stays land.
constexpr float kLocalRoom = 0.9f;

// Spurs and secondary ranges in the foothills (map intensity kSpurLow to kSpurHigh, fading out
// from kSpurCore to kSpurFull) along the lines of their ridged noise (kSpurLine to kSpurAxis).
constexpr float kSpurLow = 0.15f;
constexpr float kSpurHigh = 0.3f;
constexpr float kSpurCore = 0.5f;
constexpr float kSpurFull = 0.75f;
constexpr float kSpurLine = 0.6f;
constexpr float kSpurAxis = 0.95f;
constexpr double kSpurSharpness = 2.0;

// Ravines: the slope of the mountains is read on the first kSlopeOctaves octaves of their
// ridged noise; the ravines grow as it passes kRavineFlat to kRavineSteep (meters per meter),
// fade out near the crests (those octaves from kCrestLow to kCrestHigh) and never take more than
// kRavineRoom of the mountains.
constexpr int kSlopeOctaves = 2;
constexpr double kRavineFlat = 0.05;
constexpr double kRavineSteep = 0.2;
constexpr float kCrestLow = 0.75f;
constexpr float kCrestHigh = 0.95f;
constexpr float kRavineRoom = 0.7f;
// The ravines fade out where the noise of one of those octaves is below kDivide in absolute
// value: about a kilometer around the divides.
constexpr float kDivide = 0.1f;

// The sharpness of the ridged noise of the rock.
constexpr double kRockSharpness = 2.0;

// The faces of the walls of the rifts: their slope read over kRiftRun meters, carved by ravines
// from kRiftFaceLow to kRiftFaceHigh of it.
constexpr double kRiftRun = 50.0;
constexpr float kRiftFaceLow = 0.15f;
constexpr float kRiftFaceHigh = 0.35f;

// The coastal plain rises this many meters per meter from the shore, up to kCoastPlainTop,
// before the regional elevation takes over over GeographyParams::coastRamp.
constexpr double kCoastPlainRise = 0.002;
constexpr double kCoastPlainTop = 20.0;

// The sea floor near the coast, from the signed distance coast < 0: down to the shelf depth
// over the shelf width, then the slope.
double shelf(const GeographyParams& p, double coast) {
    const double d = -coast;
    const double depth = static_cast<double>(p.shelfDepth);
    return d <= p.shelfWidth ? depth * d / p.shelfWidth
                             : depth - (d - p.shelfWidth) * p.shelfSlope;
}

// Ground of the geography before the local relief: the shelf and the ocean floor below the
// coast, the coastal plain then the regional elevation above it. Below sea level exactly where
// the coast is negative: both sides are strictly of their sign, 0 on the coast.
float groundBase(const GeographyParams& p, const GeographySample& g) {
    if (g.coast < 0.0) {
        return std::max(g.sea, static_cast<float>(shelf(p, g.coast)));
    }
    const double plain = std::min(kCoastPlainRise * g.coast, kCoastPlainTop);
    const double t =
        noise::smoothstep(0.0f, static_cast<float>(p.coastRamp), static_cast<float>(g.coast));
    return static_cast<float>(plain + (static_cast<double>(g.land) - plain) * t);
}

} // namespace

HeightSample Heightfield::geographySample(double x, double z, Memos* memos) const {
    const GeographySample g = geography_->sampleAt(x, z);
    HeightSample s;
    s.continentalness = static_cast<float>(std::clamp(g.coast / kContinentalScale, -1.0, 1.0));
    // The local relief and the mountains, faded in from the coast: none at sea.
    const float land = noise::smoothstep(0.0f, static_cast<float>(params_.geography.coastRamp),
                                         static_cast<float>(g.coast));
    s.plateCollision = g.belt;
    const float intensity = land > 0.0f ? reliefAt(x, z, g.relief, memos) : 0.0f;
    s.relief = intensity; // 0 at sea
    s.mountainMask = land * noise::smoothstep(kMountainLow, kMountainHigh, intensity);
    // Around the regional elevation, the local relief rises and falls.
    const float ground = groundBase(params_.geography, g);
    // Under the massifs the hills give way to the ridged mountains: no smooth swell of hundreds
    // of meters under the crests.
    const float amplitude =
        land * (kPlainHills + kHillsRange * noise::smoothstep(kHillsLow, kHillsHigh, intensity)) *
        (1.0f - kHillsUnderMassifs * s.mountainMask);
    const float buttes = std::min(kButteShare * amplitude, kButteMax) *
                         (1.0f - noise::smoothstep(kButteLow, kButteHigh, intensity));
    const float plain = 1.0f - noise::smoothstep(kPlainLow, kPlainHigh, s.mountainMask);
    const float rough = noise::smoothstep(kRoughLow, kRoughHigh, intensity);
    const float local =
        amplitude > 0.0f
            ? localReliefAt(x, z, amplitude, buttes, g.regions, plain, rough, memos)
            : 0.0f;
    s.base = ground + relief_local::boundBelow(local, kLocalRoom * ground);
    if (g.rift > 0.0f && params_.geography.ravines) {
        s.base -= riftRavinesAt(x, z, kRavineRoom * std::max(0.0f, s.base - 1.0f), memos);
    }
    s.mountain = geographyMountainAt(x, z, s.mountainMask, intensity, g.character, memos);
    s.detail = detailAt(x, z, s.base, memos);
    s.height = s.base + s.mountain + s.detail;
    return s;
}

float Heightfield::reliefAt(double x, double z, float mapRelief, Memos* memos) const {
    const float window = noise::smoothstep(kSpurLow, kSpurHigh, mapRelief) *
                         (1.0f - noise::smoothstep(kSpurCore, kSpurFull, mapRelief));
    if (!(window > 0.0f)) {
        return mapRelief;
    }
    const noise::FbmOctaves& spurs = layers_->geographySpurs;
    const float line = memos != nullptr ? spurs.ridged2D(x, z, kSpurSharpness, memos->spurs)
                                        : spurs.ridged2D(x, z, kSpurSharpness);
    return std::min(1.0f, mapRelief + params_.geography.spurRelief * window *
                                          noise::smoothstep(kSpurLine, kSpurAxis, line));
}

// The ridged mountains of the geography (the warp, octaves and wavelength of the Heightfield,
// the sharpness and gain of the geography): sharp where the character is 1, worn and rounder
// (a smoothstep of the same noise, kWorn as high) where it is 0; broken by the rock (its sharp
// crests, not warped); carved by the ravines, at most kRavineRoom of their height.
float Heightfield::geographyMountainAt(double x, double z, float mask, float intensity,
                                       float character, Memos* memos) const {
    if (mask <= 0.0f) {
        return 0.0f;
    }
    const GeographyParams& p = params_.geography;
    const std::array<double, 2> warped = warpedAt(x, z, memos);
    const noise::FbmOctaves& ridged = layers_->geographyRidge;
    const noise::FbmOctaves::RidgedSlope r =
        memos != nullptr
            ? ridged.ridged2DSlope(warped[0], warped[1], p.mountainSharpness,
                                   p.mountainValleyDetail, kSlopeOctaves, memos->ridge)
            : ridged.ridged2DSlope(warped[0], warped[1], p.mountainSharpness,
                                   p.mountainValleyDetail, kSlopeOctaves);
    const float height = p.mountainHeight * noise::smoothstep(kHeightLow, kHeightHigh, intensity) *
                         (kWorn + (1.0f - kWorn) * character);
    const float worn = r.value * r.value * (3.0f - 2.0f * r.value);
    float mountain = mask * height * (character * r.value + (1.0f - character) * worn);
    if (p.rockRelief > 0.0f) {
        const noise::FbmOctaves& rock = layers_->geographyRock;
        mountain += mask * p.rockRelief *
                    (memos != nullptr ? rock.ridged2D(x, z, kRockSharpness, memos->rock)
                                      : rock.ridged2D(x, z, kRockSharpness));
    }
    if (!p.ravines) {
        return mountain;
    }
    const double amplitude = static_cast<double>(mask * height);
    const float ravines = ravinesAt(warped[0], warped[1], r, amplitude, memos);
    return mountain + relief_local::boundBelow(-ravines, kRavineRoom * mountain);
}

// The ravines run down the slope of the first octaves of the mountains, as deep as it is steep,
// fading out on the flat, near the crests and on the creases of those octaves (where the slope
// turns over: the divides).
float Heightfield::ravinesAt(double warpedX, double warpedZ,
                             const noise::FbmOctaves::RidgedSlope& ridge, double amplitude,
                             Memos* memos) const {
    const double slopeX = amplitude * ridge.headX;
    const double slopeZ = amplitude * ridge.headZ;
    const double slope = dmath::hypot(slopeX, slopeZ);
    const double strength =
        static_cast<double>(noise::smoothstep(static_cast<float>(kRavineFlat),
                                              static_cast<float>(kRavineSteep),
                                              static_cast<float>(slope))) *
        (1.0 - static_cast<double>(noise::smoothstep(kCrestLow, kCrestHigh, ridge.head))) *
        static_cast<double>(noise::smoothstep(0.0f, kDivide, ridge.crease));
    return ravines::depth(layers_->ravines, warpedX, warpedZ, slopeX, slopeZ, strength,
                          memos != nullptr ? &memos->ravines : nullptr);
}

// The ravines of the walls of the rifts run down the slope of their land (centered differences
// kRiftRun meters apart), on their faces only (slopes from kRiftFaceLow to kRiftFaceHigh).
float Heightfield::riftRavinesAt(double x, double z, float room, Memos* memos) const {
    const Geography& geography = *geography_;
    const double slopeX = static_cast<double>(geography.landAt(x + kRiftRun, z) -
                                              geography.landAt(x - kRiftRun, z)) /
                          (2.0 * kRiftRun);
    const double slopeZ = static_cast<double>(geography.landAt(x, z + kRiftRun) -
                                              geography.landAt(x, z - kRiftRun)) /
                          (2.0 * kRiftRun);
    const double strength = static_cast<double>(
        noise::smoothstep(kRiftFaceLow, kRiftFaceHigh,
                          static_cast<float>(dmath::hypot(slopeX, slopeZ))));
    if (!(strength > 0.0)) {
        return 0.0f;
    }
    const float depth = ravines::depth(layers_->ravines, x, z, slopeX, slopeZ, strength,
                                       memos != nullptr ? &memos->ravines : nullptr);
    return -relief_local::boundBelow(-depth, room);
}

float Heightfield::localReliefAt(double x, double z, float amplitude, float buttes, float region,
                                 float plain, float rough, Memos* memos) const {
    return relief_local::rolling(layers_->local, x, z, {amplitude, buttes, region, plain, rough},
                                 memos != nullptr ? &memos->local : nullptr);
}

} // namespace terrain

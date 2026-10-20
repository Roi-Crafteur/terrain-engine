#include "terrain/generation/caves.hpp"

#include "terrain/generation/cave_entrances.hpp"
#include "terrain/generation/cliffs.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/sdf.hpp"

#include <algorithm>
#include <cmath>

namespace terrain {

namespace {

// Stream of the caves, distinct from the other layers.
constexpr std::uint64_t kCaveStream = 0x43415645; // "CAVE"

// Streams of the 2D layers and of the entrances, after the 3D noises.
enum Stream : std::uint64_t {
    kEntranceStream = kCaveNoiseCount,
    kRavineStream,
    kRavinePresenceStream,
    kAquiferStream,
};

// Meters per unit of a gradient noise near its zero lines: the noise of wavelength L rises
// by about kNoiseSlope over L meters, so |n| * L / kNoiseSlope approximates the distance to
// the zero set.
constexpr double kNoiseSlope = 1.2;

// A ravine closes this far before its half-width reaches 0, so that it never leaves a seam
// of zero width.
constexpr float kRavineClosing = 0.5f;

float metersPerUnit(double wavelength) {
    return static_cast<float>(wavelength / kNoiseSlope);
}

std::array<CaveNoiseLayer, kCaveNoiseCount> noiseLayers(std::uint64_t seed,
                                                        const CaveParams& params) {
    const auto layer = [seed](CaveNoise noise, double wavelength, double verticalScale,
                              int octaves) {
        return CaveNoiseLayer{noise::deriveSeed(seed, noise), 1.0 / wavelength, verticalScale,
                              octaves};
    };
    const SpaghettiParams& s = params.spaghetti;
    const NoodleParams& n = params.noodles;
    std::array<CaveNoiseLayer, kCaveNoiseCount> layers{};
    layers[kSpaghettiA] = layer(kSpaghettiA, s.wavelength, s.verticalScale, 1);
    layers[kSpaghettiB] = layer(kSpaghettiB, s.wavelength, s.verticalScale, 1);
    layers[kTunnelRadius] = layer(kTunnelRadius, s.radiusWavelength, 1.0, 1);
    layers[kNoodleA] = layer(kNoodleA, n.wavelength, n.verticalScale, 1);
    layers[kNoodleB] = layer(kNoodleB, n.wavelength, n.verticalScale, 1);
    layers[kNoodlePresence] = layer(kNoodlePresence, n.presenceWavelength, 1.0, 1);
    layers[kCheese] = layer(kCheese, params.cheese.wavelength, 1.0, params.cheese.octaves);
    return layers;
}

// Radius of a tunnel for a value of the radius noise: the noise mostly stays within +-0.5,
// so it is doubled before being mapped onto [minRadius, maxRadius].
float tunnelRadius(float radiusNoise, float minRadius, float maxRadius) {
    const float t = std::clamp(0.5f + radiusNoise, 0.0f, 1.0f);
    return minRadius + t * (maxRadius - minRadius);
}

// Distance to the zero lines shared by a and b, in meters (scale per unit), and the void of a
// tunnel of the given radius along them.
float tunnelDistance(float a, float b, float scale) {
    return std::sqrt(a * a + b * b) * scale;
}

float tunnelVoid(float a, float b, float scale, float radius) {
    return tunnelDistance(a, b, scale) - radius;
}

// Smallest |value| over [low, high].
float smallestMagnitude(float low, float high) {
    if (low <= 0.0f && high >= 0.0f) {
        return 0.0f;
    }
    return std::min(std::abs(low), std::abs(high));
}

} // namespace

CaveField::CaveField(std::uint64_t seed, const CaveParams& params)
    : params_(params), entranceSeed_(noise::deriveSeed(noise::deriveSeed(seed, kCaveStream),
                                                       kEntranceStream)),
      ravineSeed_(noise::deriveSeed(noise::deriveSeed(seed, kCaveStream), kRavineStream)),
      ravinePresenceSeed_(
          noise::deriveSeed(noise::deriveSeed(seed, kCaveStream), kRavinePresenceStream)),
      aquiferSeed_(noise::deriveSeed(noise::deriveSeed(seed, kCaveStream), kAquiferStream)),
      lattice_(noiseLayers(noise::deriveSeed(seed, kCaveStream), params)) {
    if (params.districts) {
        districts_.emplace(seed, *params.districts);
    }
}

CaveDistrict CaveField::district(int cellX, int cellZ, const CaveGround& ground) const {
    const DistrictSite site = [&ground](double x, double z, float& height,
                                        CaveEnvironment& environment) {
        height = ground.height(x, z);
        environment = ground.environment ? ground.environment(x, z) : CaveEnvironment{};
    };
    return districts_->district(cellX, cellZ, site);
}

CaveColumn CaveField::column(double x, double z, float ground, const CaveGround& relief,
                             std::span<const CaveEntrance> entrances, AquiferCache* cells,
                             bool aquifer) const {
    CaveColumn column;
    column.x = x;
    column.z = z;
    column.ground = ground;
    if (districts_) {
        AquiferCache alone(*this, relief);
        column.profile = (cells != nullptr ? cells : &alone)->profileAt(x, z);
    }
    const float river = relief.riverDistance ? relief.riverDistance(x, z) : kNoRiverDistance;
    const float seaDepth = kSeaLevel - ground;
    column.exclusion = std::max(params_.riverClearance - river, seaDepth - params_.deepOceanDepth);

    const RavineParams& ravines = params_.ravines;
    const double frequency = 1.0 / ravines.wavelength;
    const float axis = noise::gradient2D(x * frequency, z * frequency, ravineSeed_);
    column.ravineDistance = std::abs(axis) * metersPerUnit(ravines.wavelength);
    const noise::FbmParams presence{2, 1.0 / ravines.presenceWavelength, 2.0, 0.5};
    column.ravineStrength = noise::smoothstep(
        ravines.presenceLow, ravines.presenceHigh,
        noise::fbm2D(x, z, ravinePresenceSeed_, presence));
    column.ravineStrength =
        std::min(1.0f, column.ravineStrength + column.profile[CaveProfile::kRavineBoost]);

    for (std::size_t i = 0; i < entrances.size(); ++i) {
        if (column.entranceCount == kMaxColumnEntrances) {
            break;
        }
        if (entranceAxisDistance(entrances[i], x, z) <= entranceReach(entrances[i])) {
            column.entrances[static_cast<std::size_t>(column.entranceCount)] = static_cast<int>(i);
            ++column.entranceCount;
        }
    }
    if (cells != nullptr && aquifer) {
        addAquifer(column, *cells);
    }
    return column;
}

float CaveField::limit(const CaveColumn& column, double y) const {
    const auto depth = static_cast<float>(static_cast<double>(column.ground) - y);
    return std::max({params_.minRoof - depth, depth - params_.maxDepth, column.exclusion});
}

unsigned CaveField::noisesOf(const CaveDetail& detail) const {
    unsigned noises = 0;
    if (detail.has(kCaveSpaghetti)) {
        noises |= caveNoiseBit(kSpaghettiA) | caveNoiseBit(kSpaghettiB) |
                  caveNoiseBit(kTunnelRadius);
    }
    if (detail.has(kCaveNoodles)) {
        noises |= caveNoiseBit(kNoodleA) | caveNoiseBit(kNoodleB) |
                  caveNoiseBit(kNoodlePresence) | caveNoiseBit(kTunnelRadius);
    }
    if (detail.has(kCaveCheese)) {
        noises |= caveNoiseBit(kCheese);
    }
    return noises;
}

CaveCellBound CaveField::cellBound(const CaveNoiseValues& low, const CaveNoiseValues& high,
                                   const CaveDetail& detail) const {
    CaveCellBound bound;
    if (detail.has(kCaveSpaghetti)) {
        const SpaghettiParams& s = params_.spaghetti;
        const float a = smallestMagnitude(low[kSpaghettiA], high[kSpaghettiA]);
        const float b = smallestMagnitude(low[kSpaghettiB], high[kSpaghettiB]);
        bound.spaghettiDistance = tunnelDistance(a, b, metersPerUnit(s.wavelength));
        bound.spaghettiRadius = tunnelRadius(high[kTunnelRadius], s.minRadius, s.maxRadius);
    }
    if (detail.has(kCaveNoodles)) {
        const NoodleParams& n = params_.noodles;
        const float a = smallestMagnitude(low[kNoodleA], high[kNoodleA]);
        const float b = smallestMagnitude(low[kNoodleB], high[kNoodleB]);
        bound.noodleDistance = tunnelDistance(a, b, metersPerUnit(n.wavelength));
        bound.noodleRadius = tunnelRadius(high[kTunnelRadius], n.minRadius, n.maxRadius);
        bound.presenceHigh = high[kNoodlePresence];
    }
    if (detail.has(kCaveCheese)) {
        bound.cheeseHigh = high[kCheese];
    }
    return bound;
}

// The same expressions as voids, on the extremes of the cell: every term only grows with the
// distances and shrinks with the radius, the presence and the cheese noise.
float CaveField::noiseLowerBound(const CaveCellBound& cell, const CaveProfile& profile,
                                 const CaveDetail& detail) const {
    float bound = params_.voidClamp;
    if (detail.has(kCaveSpaghetti)) {
        const float radius = cell.spaghettiRadius * profile[CaveProfile::kSpaghettiScale];
        bound = std::min(bound, cell.spaghettiDistance - radius);
    }
    if (detail.has(kCaveNoodles)) {
        const NoodleParams& n = params_.noodles;
        const float tube = cell.noodleDistance - cell.noodleRadius;
        const float presence = n.presenceThreshold + profile[CaveProfile::kNoodleOffset];
        const float absent = (presence - cell.presenceHigh) * metersPerUnit(n.presenceWavelength);
        bound = std::min(bound, std::max(tube, absent));
    }
    if (detail.has(kCaveCheese)) {
        const CheeseParams& c = params_.cheese;
        const float threshold = c.threshold + profile[CaveProfile::kCheeseOffset];
        bound = std::min(bound, (threshold - cell.cheeseHigh) * metersPerUnit(c.wavelength));
    }
    return bound;
}

float CaveField::noiseLowerBound(const CaveNoiseValues& low, const CaveNoiseValues& high,
                                 const CaveDetail& detail) const {
    return noiseLowerBound(cellBound(low, high, detail), CaveProfile{}, detail);
}

CaveVoids CaveField::voids(const CaveColumn& column, double y, const CaveNoiseValues& noise,
                           std::span<const CaveEntrance> entrances,
                           const CaveDetail& detail) const {
    const float clamp = params_.voidClamp;
    const auto bounded = [clamp](float value) { return std::clamp(value, -clamp, clamp); };
    const auto depth = static_cast<float>(static_cast<double>(column.ground) - y);
    CaveVoids result{clamp, clamp, clamp, clamp, clamp, 0.0f, 0.0f};
    if (detail.has(kCaveSpaghetti)) {
        const SpaghettiParams& s = params_.spaghetti;
        const float radius = tunnelRadius(noise[kTunnelRadius], s.minRadius, s.maxRadius) *
                             column.profile[CaveProfile::kSpaghettiScale];
        result.spaghetti = bounded(tunnelVoid(noise[kSpaghettiA], noise[kSpaghettiB],
                                              metersPerUnit(s.wavelength), radius));
    }
    if (detail.has(kCaveNoodles)) {
        const NoodleParams& n = params_.noodles;
        const float radius = tunnelRadius(noise[kTunnelRadius], n.minRadius, n.maxRadius);
        const float tube =
            tunnelVoid(noise[kNoodleA], noise[kNoodleB], metersPerUnit(n.wavelength), radius);
        const float presence = n.presenceThreshold + column.profile[CaveProfile::kNoodleOffset];
        const float absent = (presence - noise[kNoodlePresence]) *
                             metersPerUnit(n.presenceWavelength);
        result.noodles = bounded(std::max(tube, absent));
    }
    if (detail.has(kCaveCheese)) {
        const CheeseParams& c = params_.cheese;
        const float threshold = c.threshold + column.profile[CaveProfile::kCheeseOffset];
        const float cavern = (threshold - noise[kCheese]) * metersPerUnit(c.wavelength);
        result.cheese = bounded(std::max(cavern, c.minDepth - depth));
    }
    if (detail.has(kCaveRavines)) {
        result.ravines = bounded(ravineVoid(column, y));
    }
    if (detail.has(kCaveEntrances) && column.entranceCount > 0) {
        result.entrances = bounded(entranceVoid(column, y, entrances));
    }
    result.limit = limit(column, y);
    result.entranceLimit = std::max(depth - params_.maxDepth, column.exclusion);
    result.barrier = detail.has(kCaveAquifers) ? barrierVoid(column, y) : -clamp;
    result.galleries = clamp;
    result.bank = -clamp;
    result.sill = -clamp;
    return result;
}

float CaveField::limitedEntranceVoid(const CaveColumn& column, double y,
                                     std::span<const CaveEntrance> entrances) const {
    const auto depth = static_cast<float>(static_cast<double>(column.ground) - y);
    const float limit = std::max(depth - params_.maxDepth, column.exclusion);
    const float entrance = std::clamp(entranceVoid(column, y, entrances), -params_.voidClamp,
                                      params_.voidClamp);
    return std::max(entrance, limit);
}

// Without galleries, banks and sills (+voidClamp, -voidClamp, -voidClamp) every min and max
// below leaves the clamped voids exactly as they are.
float CaveField::combine(const CaveVoids& voids) const {
    const float others =
        std::max(std::min({voids.spaghetti, voids.noodles, voids.cheese, voids.ravines}),
                 voids.bank);
    const float inner = std::max(std::min(others, voids.galleries), voids.sill);
    const float limited = std::max({inner, voids.limit, voids.barrier});
    const float entrance =
        std::max({voids.entrances, voids.entranceLimit, voids.barrier, voids.bank});
    return std::clamp(std::min(limited, entrance), -params_.voidClamp, params_.voidClamp);
}

float CaveField::waterDensity(const CaveColumn& column, double y, float terrain,
                              float carved) const {
    const AquiferParams& a = params_.aquifers;
    if (column.waterLevel <= kDryLevel) {
        return params_.voidClamp;
    }
    const auto belowLevel = static_cast<float>(y - static_cast<double>(column.waterLevel));
    return std::min(std::max({belowLevel, -carved - a.skin, terrain + a.groundMargin}),
                    params_.voidClamp);
}

float CaveField::carve(float terrain, float caveVoid, const CaveDetail& detail) const {
    const float end = detail.guard + params_.fade;
    if (caveVoid >= end) {
        return terrain;
    }
    const float carved = sdf::smoothSubtract(terrain, caveVoid, params_.blend);
    if (caveVoid <= detail.guard) {
        return carved;
    }
    const float t = noise::smoothstep(detail.guard, end, caveVoid);
    return std::max(terrain, carved + t * (terrain - carved));
}

float CaveField::ravineVoid(const CaveColumn& column, double y) const {
    const RavineParams& r = params_.ravines;
    if (column.ravineStrength <= 0.0f) {
        return params_.voidClamp;
    }
    const auto depth = static_cast<float>(static_cast<double>(column.ground) - y);
    const float bottom = params_.minRoof + r.height;
    const float t = std::clamp((depth - params_.minRoof) / r.height, 0.0f, 1.0f);
    const float halfWidth = r.maxHalfWidth * column.ravineStrength * (1.0f - t) - kRavineClosing;
    return std::max(column.ravineDistance - halfWidth, depth - bottom);
}

} // namespace terrain

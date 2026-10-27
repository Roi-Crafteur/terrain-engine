// The last step of a sample of the Heightfield (phase 11B): the depressions below the sea level
// and the water over the point.
#include "terrain/generation/heightfield.hpp"

#include "depressions.hpp"
#include "heightfield_layers.hpp"
#include "ravines.hpp"
#include "relief_local.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

namespace terrain {

struct Heightfield::Water {
    std::optional<depressions::Sites> depressions;
};

namespace {

// The coast distance of a sample of the geography, from its continentalness (as
// heightfield_geography.cpp scales it, exact enough below the scale).
constexpr double kContinentalScale = 1'500'000.0;

// The depressions fade out as the intensity of the relief rises from kDepressionLow to
// kDepressionHigh, where the mountains begin (their mask is 0 below it: they keep their bits),
// and within kCoastLow to kCoastHigh meters of the sea.
constexpr float kDepressionLow = 0.09f;
constexpr float kDepressionHigh = 0.12f;
constexpr float kCoastLow = 150'000.0f;
constexpr float kCoastHigh = 250'000.0f;

// On the floor of a depression the ground keeps kKeep of the relief of the plains (the local relief
// over the regional elevation): almost flat, never a plane.
constexpr double kKeep = 0.35;
constexpr double kHead = 1.5;

void setSea(HeightSample& s, bool sea) {
    s.water = sea ? WaterKind::Ocean : WaterKind::None;
    s.waterLevel = sea ? kSeaLevel : s.height;
}

// The ground in a depression, and how flat it is there (1 in a salt pan: no detail).
struct Lowered {
    double ground = 0.0;
    double flat = 0.0;
};

// The ground base meters high at (x, z) brought weight of the way into the depression of basin:
// toward its floor, which keeps kKeep of the local relief over the regional elevation land, with
// its salt pans and buttes; then carved by the ravines of the relief down its sides.
Lowered inDepression(const depressions::Sites& sites, const ravines::Network& network, double x,
                     double z, double base, double land, double weight,
                     const depressions::Basin& basin) {
    const depressions::Profile& p = basin.profile;
    double floor = basin.floor + kKeep * (base - land);
    const double t = weight * p.share;
    Lowered out;
    if (p.onFloor > 0.0) {
        const depressions::Floor f = depressions::floorAt(sites.forms, x, z);
        floor += p.onFloor * f.butte;
        floor += p.onFloor * f.pan * (basin.floor - depressions::kPanDepth - floor);
        out.flat = t * p.onFloor * f.pan;
    }
    double ground = base + t * (floor - base);
    // The ravines grow from nothing at the rim: at most kHead times the drop of the profile there.
    const double room = weight * std::min(p.room, kHead * p.share * (base - basin.floor));
    const double strength = weight * (1.0 - p.onFloor);
    if (room > 0.0 && strength > 0.0 && p.slope > 0.0) {
        const double slope = weight * p.slope;
        const float cut = ravines::depth(network, x, z, slope * p.upX, slope * p.upZ, strength);
        ground += static_cast<double>(relief_local::boundBelow(-cut, static_cast<float>(room)));
    }
    out.ground = ground;
    return out;
}

} // namespace

std::shared_ptr<const Heightfield::Water> Heightfield::makeWater(std::uint64_t seed,
                                                                const HeightfieldParams& params,
                                                                const Geography* geography) {
    auto water = std::make_shared<Water>();
    if (geography != nullptr && params.geography.depressions) {
        water->depressions = depressions::makeSites(seed, *geography);
    }
    return water;
}

std::vector<Depression> Heightfield::depressions() const {
    std::vector<Depression> out;
    if (water_->depressions) {
        for (const depressions::Site& s : water_->depressions->sites) {
            out.push_back({s.x, s.z, s.radius, static_cast<float>(s.floor), s.steepSide});
        }
    }
    return out;
}

// With the geography the sea is where its coast signal is below 0 (the sign of the
// continentalness), whatever the height: a depression below the sea level deep inland stays dry
// land. Without it, the ground below the sea level. The hydrology comes after (applyHydrology).
void Heightfield::finishWater(double x, double z, HeightSample& s) const {
    if (!geography_) {
        setSea(s, s.height < kSeaLevel);
        return;
    }
    if (water_->depressions && s.relief < kDepressionHigh && s.continentalness > 0.0f &&
        depressions::mayReach(*water_->depressions, x, z)) {
        const auto coast = static_cast<float>(static_cast<double>(s.continentalness) *
                                              kContinentalScale);
        const float weight = (1.0f - noise::smoothstep(kDepressionLow, kDepressionHigh, s.relief)) *
                             noise::smoothstep(kCoastLow, kCoastHigh, coast);
        if (weight > 0.0f) {
            const depressions::Basin basin = depressions::basinAt(
                *water_->depressions, x, z, static_cast<double>(s.base));
            if (basin.profile.share > 0.0) {
                const Lowered lowered = inDepression(
                    *water_->depressions, layers_->ravines, x, z, static_cast<double>(s.base),
                    static_cast<double>(geography_->landAt(x, z)), static_cast<double>(weight),
                    basin);
                s.base = static_cast<float>(lowered.ground);
                s.detail = static_cast<float>(static_cast<double>(s.detail) * (1.0 - lowered.flat));
                s.height = s.base + s.mountain + s.detail;
            }
        }
    }
    setSea(s, s.continentalness < 0.0f);
}

} // namespace terrain

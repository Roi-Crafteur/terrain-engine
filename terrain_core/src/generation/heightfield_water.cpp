// The last step of a sample of the Heightfield (phase 11B): the depressions below the sea level
// and the water over the point.
#include "terrain/generation/heightfield.hpp"

#include "depressions.hpp"
#include "terrain/generation/noise.hpp"

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

void setSea(HeightSample& s, bool sea) {
    s.water = sea ? WaterKind::Ocean : WaterKind::None;
    s.waterLevel = sea ? kSeaLevel : s.height;
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
// land. Without it, the ground below the sea level.
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
            const depressions::Basin basin = depressions::basinAt(*water_->depressions, x, z);
            if (basin.share > 0.0) {
                const double t = static_cast<double>(weight) * basin.share;
                s.base = static_cast<float>(static_cast<double>(s.base) +
                                            t * (basin.floor - static_cast<double>(s.base)));
                s.height = s.base + s.mountain + s.detail;
            }
        }
    }
    setSea(s, s.continentalness < 0.0f);
}

} // namespace terrain

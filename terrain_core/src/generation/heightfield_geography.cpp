#include "terrain/generation/heightfield.hpp"

#include "relief_local.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cmath>

namespace terrain {

namespace {

// Distance to the coast at which the continentalness reaches 1: as before, the coast at 0 and
// the deep interior beyond 0.6 for the climate.
constexpr double kContinentalScale = 1'500'000.0;

// The mountain mask rises from kBeltStart to full at kBeltFull of the belt: the weakest belts are
// as rugged as the others, only lower (their uplift); their faint tails carry none.
constexpr float kBeltStart = 0.05f;
constexpr float kBeltFull = 0.5f;

// The local relief has this amplitude everywhere on land (meters), plus kProvinceHills times the
// hills of the provinces (their height above their valleys): never flat.
constexpr float kPlainHills = 55.0f;
constexpr float kProvinceHills = 0.5f;

// Buttes kButteShare of the amplitude high, at most kButteMax meters, away from the mountains.
constexpr float kButteShare = 0.8f;
constexpr float kButteMax = 50.0f;

// The local relief never takes more than this share of the ground above the sea level away
// from it: the land stays land.
constexpr float kLocalRoom = 0.9f;

// The low mountains of the old massifs and intraplate high relief of the provinces: kMassifHeight
// times their massif beyond kMassifStart (their faint tails carry none).
constexpr float kMassifHeight = 0.45f;
constexpr float kMassifStart = 0.05f;

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
    // The local relief and the mountains of the belts and provinces, faded in from the coast:
    // none at sea.
    const float land = noise::smoothstep(0.0f, static_cast<float>(params_.geography.coastRamp),
                                         static_cast<float>(g.coast));
    s.plateCollision = g.belt;
    s.mountainMask =
        land * std::max(noise::smoothstep(kBeltStart, kBeltFull, g.belt),
                        kMassifHeight * std::max(0.0f, g.massif - kMassifStart) /
                            (1.0f - kMassifStart));
    // Around the regional elevation, the local relief rises and falls.
    const float ground = groundBase(params_.geography, g);
    const float amplitude = land * (kPlainHills + kProvinceHills * std::max(0.0f, g.hills));
    const float buttes =
        std::min(kButteShare * amplitude, kButteMax) * (1.0f - s.mountainMask);
    const float local = amplitude > 0.0f ? localReliefAt(x, z, amplitude, buttes, memos) : 0.0f;
    s.base = ground + relief_local::boundBelow(local, kLocalRoom * ground);
    s.mountain = geographyMountainAt(x, z, s.mountainMask, memos);
    s.detail = detailAt(x, z, s.base, memos);
    s.height = s.base + s.mountain + s.detail;
    return s;
}

} // namespace terrain

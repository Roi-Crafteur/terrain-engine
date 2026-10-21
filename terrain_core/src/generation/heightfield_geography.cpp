#include "terrain/generation/heightfield.hpp"

#include "terrain/det_math.hpp"
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

// Heights above kSoftTop are squeezed by kSoftSqueeze: summits stay below about 8,500 m where
// mountains stand on high plateaus.
constexpr float kSoftTop = 6500.0f;
constexpr float kSoftSqueeze = 0.55f;

// The hills of the provinces start from this amplitude (meters).
constexpr float kMinHills = 20.0f;

// The detail is at most the regional slope times this (half the cells of the hydrology).
constexpr double kDetailRun = 4000.0;

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
    // The hills and mountains of the belts and provinces, faded in from the coast: none at sea.
    const float land = noise::smoothstep(0.0f, static_cast<float>(params_.geography.coastRamp),
                                         static_cast<float>(g.coast));
    // Hills from kMinHills of amplitude (below, none: no valley to compute).
    const float hills = land * std::max(0.0f, g.hills - kMinHills);
    s.plateCollision = g.belt;
    s.mountainMask =
        land * std::max(noise::smoothstep(kBeltStart, kBeltFull, g.belt),
                        kMassifHeight * std::max(0.0f, g.massif - kMassifStart) /
                            (1.0f - kMassifStart));
    // The valleys of the drainage of the map, under the hills and the mountains (steeper).
    const ValleyShape valleys =
        hills > 0.0f || s.mountainMask > 0.0f ? geography_->valleyShape(x, z) : ValleyShape{};
    // The hills of the provinces: their amplitude between the valleys.
    s.base = groundBase(params_.geography, g) + hills * valleys.hills;
    s.mountain = geographyMountainAt(x, z, s.mountainMask, g, valleys.mountains, memos);
    // Summits above kSoftTop rise half as fast (a monotone squeeze: no new hollow).
    const float top = s.base + s.mountain - kSoftTop;
    if (top > 0.0f) {
        s.mountain -= kSoftSqueeze * top;
    }
    // The detail, at most the regional slope times kDetailRun: on flat land it would fill the
    // cells of the hydrology with puddles.
    const double slope = dmath::hypot(static_cast<double>(g.landDX), static_cast<double>(g.landDZ));
    const float flat = static_cast<float>(
        std::min(1.0, slope * kDetailRun / static_cast<double>(params_.detailAmplitude)));
    s.detail = flat * detailAt(x, z, s.base, memos);
    s.height = s.base + s.mountain + s.detail;
    return s;
}

} // namespace terrain

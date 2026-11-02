// The prevailing wind of the ergs (ergs_impl.hpp, windOf).
#include "ergs_impl.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace terrain {

namespace {

// The bands of the winds join over kBandJoin degrees on each side of 30 and 60 degrees; the
// trades of the two hemispheres over kEquatorJoin degrees on each side of the equator.
constexpr double kBandJoin = 4.0;
constexpr double kEquatorJoin = 2.0;
// The wind turns by up to kTurnDegrees with a slow noise.
constexpr double kTurnDegrees = 18.0;
constexpr double kTurnRadians = 6.283185307179586;

double hermite(double edge0, double edge1, double v) {
    const double t = std::clamp((v - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// The angle (turns) toward which a band of winds blows: zonal along x, meridional poleward.
double bandAngle(double zonal, double meridional, double poleward) {
    return dmath::atan2(poleward * meridional, zonal) / kTurnRadians;
}

double shorter(double turns) {
    return turns - std::floor(turns + 0.5);
}

} // namespace

std::array<double, 2> windOf(const Ergs::Impl& m, double x, double z) {
    const double latitude = 180.0 * std::abs(z) / m.world.z;
    const double westerly = hermite(30.0 - kBandJoin, 30.0 + kBandJoin, latitude);
    const double polar = hermite(60.0 - kBandJoin, 60.0 + kBandJoin, latitude);
    // Poleward is -z in the north (z < 0), +z in the south, joined across the equator.
    const double poleward =
        2.0 * hermite(-kEquatorJoin * m.degree, kEquatorJoin * m.degree, z) - 1.0;
    // The angles of the bands, joined along the shorter arc: the wind turns at an even pace
    // across the joins, never through a calm.
    const double trades = bandAngle(-1.0, m.climate.tradeMeridional, poleward);
    const double westerlies = bandAngle(1.0, m.climate.westerlyMeridional, poleward);
    const double polars = bandAngle(-1.0, m.climate.polarMeridional, poleward);
    const double angle = trades + shorter(westerlies - trades) * westerly +
                         shorter(polars - westerlies) * polar +
                         kTurnDegrees / 360.0 * static_cast<double>(m.turn.fbm2D(x, z));
    const dmath::SineCosine d = dmath::sinCosTurns(angle);
    return {d.cosine, d.sine};
}

} // namespace terrain

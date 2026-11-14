#include "atmosphere_haze.hpp"

#include "terrain/generation/planet/planet_level.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace terrain::unreal {

namespace {

double smoothstep(double from, double to, double v) {
    const double t = std::clamp((v - from) / (to - from), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

// The rings of the region: the point, then 8 points at 12 km and 16 at 30 km.
struct Ring {
    int points;
    double radius;
};
constexpr std::array<Ring, 3> kRings{{{1, 0.0}, {8, 12'000.0}, {16, 30'000.0}}};

} // namespace

HazeLook hazeLook(const HazeLook& map, const DustHaze& dust, double dryness) {
    const double d = std::clamp(dryness, 0.0, 1.0);
    HazeLook out;
    out.viewDistanceScale = map.viewDistanceScale * lerp(1.0, dust.viewDistanceScale, d);
    out.mieScatteringScale = map.mieScatteringScale * lerp(1.0, dust.mieScattering, d);
    out.mieAbsorptionScale = lerp(map.mieAbsorptionScale, dust.mieAbsorptionScale, d);
    for (std::size_t c = 0; c < 3; ++c) {
        out.mieAbsorption[c] = lerp(map.mieAbsorption[c], dust.mieAbsorption[c], d);
    }
    return out;
}

double climateDryness(double humidity, double temperature, double elevation) {
    return (1.0 - smoothstep(0.15, 0.40, humidity)) * smoothstep(-6.0, 2.0, temperature) *
           smoothstep(-200.0, 0.0, elevation);
}

double pointDryness(const PlanetLevel& planet, double x, double z) {
    const PlanetSample s = planet.sampleAt(x, z);
    return climateDryness(planet.humidityAt(x, z), s.temperature, s.elevation);
}

double regionDryness(const PlanetLevel& planet, double x, double z) {
    double sum = 0.0;
    int count = 0;
    for (const Ring& ring : kRings) {
        for (int n = 0; n < ring.points; ++n) {
            const double a = 2.0 * std::numbers::pi * n / ring.points;
            sum += pointDryness(planet, x + ring.radius * std::sin(a),
                                z - ring.radius * std::cos(a));
            ++count;
        }
    }
    return sum / count;
}

double HazeFollower::follow(double target, double dt, double x, double z) {
    const bool jumped = std::hypot(x - x_, z - z_) > kHazeJumpMeters;
    x_ = x;
    z_ = z;
    if (!started_ || jumped) {
        started_ = true;
        shown_ = target;
        return shown_;
    }
    shown_ += (target - shown_) * (1.0 - std::exp(-std::max(dt, 0.0) / kHazeSeconds));
    return shown_;
}

} // namespace terrain::unreal

// The haze of the dust over dry land (Bridge/atmosphere_haze.hpp, agent 40), on the default world
// with its planet file:
// - the dryness of the region of the 7 views compared in Unreal: about 1 over the deserts (the
//   erg of great waves from 8 km and from its crest, giants_B, the cold desert), about 0 over the
//   humid lands (a humid chain seen from 50 km, the open sea from a beach, a rain forest);
// - the look of the map exactly at d = 0, the dusty air at 1, monotonic between;
// - the dryness along 400 km from the erg northward, from a desert to a humid land: never more
//   than 0.05 per km; the dryness shown following it in time, at once after a teleport;
// - the sun through the dusty air: the transmittance of the atmosphere toward the sun, as Unreal
//   computes it on the CPU from the parameters of the SkyAtmosphere and applies it to the light of
//   the sun and to its disc (Rayleigh over 8 km, Mie over 1.2 km, the ozone in a tent at 25 +- 15
//   km, a curved planet of the radius of the plugin, from the ground: the most dust), off (the
//   defaults of Unreal) and auto (d = 1): at most 2 % per channel and 1 % of tint (blue over red)
//   from 7 degrees up, so that the near sand and the sun keep their color. Printed for the report:
//   the ratios, and a sand (192, 151, 97) lit by the sun alone. The part of the sky (the SkyLight
//   captures a paler sky) is measured in Unreal.
#include "coast_planet_support.hpp"

#include "atmosphere_haze.hpp"
#include "horizon_layout.hpp"
#include "test_assert.hpp"

#include "terrain/generation/planet/planet_level.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace {

using terrain::unreal::DustHaze;
using terrain::unreal::HazeLook;

struct View {
    const char* name;
    double x, z;
    bool dry;
};

constexpr std::array<View, 7> kViews{{{"erg_vagues_8km", 11505334.0, -4967544.0, true},
                                      {"giants_B", -323600.0, 3960700.0, true},
                                      {"erg_vagues_crete", 11505016.0, -4964964.0, true},
                                      {"desert_froid_500m", 1863968.0, -10006000.0, true},
                                      {"chaine_humide_50km", 4125500.0, 2555832.0, false},
                                      {"plage_origine_est", 3400.0, 0.0, false},
                                      {"foret_humide_sol", -6179604.0, 165924.0, false}}};

const terrain::PlanetLevel& planet() {
    return coast_planet::field().hydrology()->planet();
}

void testViews() {
    for (const View& v : kViews) {
        const double d = terrain::unreal::regionDryness(planet(), v.x, v.z);
        std::printf("dryness of %s: %.3f\n", v.name, d);
        TEST_CHECK(v.dry ? d > 0.9 : d < 0.05);
    }
}

void testLook() {
    const HazeLook map{};
    const DustHaze dust{};
    TEST_CHECK(terrain::unreal::hazeLook(map, dust, 0.0) == map);
    TEST_CHECK(terrain::unreal::hazeLook(map, dust, -1.0) == map);
    const HazeLook full = terrain::unreal::hazeLook(map, dust, 1.0);
    TEST_CHECK(full.viewDistanceScale == map.viewDistanceScale * dust.viewDistanceScale &&
               full.mieScatteringScale == map.mieScatteringScale * dust.mieScattering &&
               full.mieAbsorptionScale == dust.mieAbsorptionScale &&
               full.mieAbsorption == dust.mieAbsorption);
    TEST_CHECK(terrain::unreal::hazeLook(map, dust, 2.0) == full);
    HazeLook before = map;
    for (int n = 1; n <= 100; ++n) {
        const HazeLook now = terrain::unreal::hazeLook(map, dust, n / 100.0);
        TEST_CHECK(now.viewDistanceScale <= before.viewDistanceScale &&
                   now.mieScatteringScale >= before.mieScatteringScale &&
                   now.mieAbsorption[0] <= before.mieAbsorption[0] &&
                   now.mieAbsorption[2] == before.mieAbsorption[2]);
        before = now;
    }
}

// From the erg of great waves 400 km northward: a desert, then a humid land.
void testWalk() {
    const double x = 11505334.0, z = -4967544.0;
    double previous = terrain::unreal::regionDryness(planet(), x, z);
    const double first = previous;
    double steepest = 0.0;
    for (int km = 1; km <= 400; ++km) {
        const double d = terrain::unreal::regionDryness(planet(), x, z - 1000.0 * km);
        steepest = std::max(steepest, std::abs(d - previous));
        previous = d;
    }
    std::printf("from the erg 400 km northward: dryness %.3f to %.3f, at most %.4f per km\n",
                first, previous, steepest);
    TEST_CHECK(first > 0.9 && previous < 0.1 && steepest <= 0.05);
}

void testFollower() {
    terrain::unreal::HazeFollower f;
    TEST_CHECK(f.follow(1.0, 0.0, 0.0, 0.0) == 1.0); // the first value at once
    const double after = f.follow(0.0, terrain::unreal::kHazeSeconds, 100.0, 0.0);
    TEST_CHECK(std::abs(after - std::exp(-1.0)) < 1e-12);
    const double teleport = 100.0 + terrain::unreal::kHazeJumpMeters + 1.0;
    TEST_CHECK(f.follow(0.0, 0.016, teleport, 0.0) == 0.0);
    double shown = f.shown();
    for (int frame = 0; frame < 600; ++frame) { // 10 s toward 1 at 60 Hz, never a jump
        const double next = f.follow(1.0, 1.0 / 60.0, teleport + frame, 0.0);
        TEST_CHECK(next >= shown && next - shown < 0.01);
        shown = next;
    }
    TEST_CHECK(shown > 0.95);
}

// The transmittance of the atmosphere toward a sun that many degrees high, from the ground.
std::array<double, 3> sunTransmittance(const HazeLook& look, double elevation) {
    constexpr std::array<double, 3> kRayleigh{0.005802, 0.013558, 0.0331}; // 1/km
    constexpr std::array<double, 3> kOzone{0.000650, 0.001881, 0.000085};
    constexpr double kTop = 60.0; // km, the AtmosphereHeight of Unreal
    const double radius = terrain::unreal::planetRadiusMeters(terrain::WorldSize{}) / 1000.0;
    const double mu = std::sin(elevation * std::numbers::pi / 180.0);
    const double length = -radius * mu + std::sqrt(radius * radius * mu * mu +
                                                   (radius + kTop) * (radius + kTop) -
                                                   radius * radius);
    constexpr int kSteps = 4000;
    const double ds = length / kSteps;
    std::array<double, 3> depth{};
    for (int i = 0; i < kSteps; ++i) {
        const double s = (i + 0.5) * ds;
        const double h = std::sqrt(radius * radius + s * s + 2.0 * radius * s * mu) - radius;
        const double rayleigh = std::exp(-h / 8.0);
        const double mie = std::exp(-h / 1.2);
        const double ozone = std::max(0.0, 1.0 - std::abs(h - 25.0) / 15.0);
        for (std::size_t c = 0; c < 3; ++c) {
            depth[c] += ds * (kRayleigh[c] * rayleigh +
                              (look.mieScatteringScale +
                               look.mieAbsorptionScale * look.mieAbsorption[c]) * mie +
                              kOzone[c] * ozone);
        }
    }
    return {std::exp(-depth[0]), std::exp(-depth[1]), std::exp(-depth[2])};
}

void testSun() {
    const HazeLook off{};
    const HazeLook dusty = terrain::unreal::hazeLook(off, DustHaze{}, 1.0);
    for (const double elevation : std::array{7.0, 15.0, 35.0}) {
        const auto a = sunTransmittance(off, elevation);
        const auto b = sunTransmittance(dusty, elevation);
        const std::array<double, 3> r{b[0] / a[0], b[1] / a[1], b[2] / a[2]};
        const double tint = r[2] / r[0];
        std::printf("sun at %.0f degrees through the dust: x (%.4f, %.4f, %.4f), tint blue/red "
                    "x %.4f; a sand lit by it (192, 151, 97) -> (%.1f, %.1f, %.1f)\n",
                    elevation, r[0], r[1], r[2], tint, 192.0 * r[0], 151.0 * r[1], 97.0 * r[2]);
        for (const double channel : r) {
            TEST_CHECK(channel <= 1.0 + 1e-12 && channel >= 0.98);
        }
        TEST_CHECK(std::abs(tint - 1.0) <= 0.01);
    }
}

} // namespace

int main() {
    testViews();
    testLook();
    testWalk();
    testFollower();
    testSun();
    return test::result();
}

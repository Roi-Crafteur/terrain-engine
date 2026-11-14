// The shadows of the GPU horizon under a low sun (Bridge/horizon_low_sun.hpp, agent 40): the
// elevation of the sun from the direction of its light as Terrain.Sun turns it, and the rule with
// its hysteresis: on under the threshold, off above it plus one degree, never blinking for a sun
// that wavers around it, never on with a threshold of 0.
#include "horizon_low_sun.hpp"

#include "test_assert.hpp"

#include <array>
#include <cmath>
#include <numbers>

namespace {

using terrain::unreal::kLowSunShadowLevels;
using terrain::unreal::lowSunShadowLevels;

constexpr double kDegree = std::numbers::pi / 180.0;

// The direction of a light turned to FRotator(pitch, yaw, 0), as FRotator::Vector gives it.
void lightDirection(double pitch, double yaw, double& x, double& y, double& z) {
    x = std::cos(pitch * kDegree) * std::cos(yaw * kDegree);
    y = std::cos(pitch * kDegree) * std::sin(yaw * kDegree);
    z = std::sin(pitch * kDegree);
}

void testElevation() {
    for (const double elevation : std::array{-5.0, 0.0, 7.0, 15.0, 35.0, 89.0}) {
        for (const double heading : std::array{0.0, 90.0, 254.0, 330.0}) {
            double x = 0.0, y = 0.0, z = 0.0;
            lightDirection(-elevation, heading + 90.0, x, y, z); // Terrain.Sun elevation heading
            TEST_CHECK(std::abs(terrain::unreal::sunElevationDegrees(x, y, z) - elevation) < 1e-9);
            TEST_CHECK(std::abs(terrain::unreal::sunElevationDegrees(3 * x, 3 * y, 3 * z) -
                                elevation) < 1e-9);
        }
    }
    TEST_CHECK(terrain::unreal::sunElevationDegrees(0.0, 0.0, 0.0) == 0.0);
}

void testRule() {
    TEST_CHECK(lowSunShadowLevels(7.0, 11.0, 0) == kLowSunShadowLevels);
    TEST_CHECK(lowSunShadowLevels(15.0, 11.0, kLowSunShadowLevels) == 0);
    TEST_CHECK(lowSunShadowLevels(11.5, 11.0, kLowSunShadowLevels) == kLowSunShadowLevels);
    TEST_CHECK(lowSunShadowLevels(11.5, 11.0, 0) == 0);
    TEST_CHECK(lowSunShadowLevels(12.5, 11.0, kLowSunShadowLevels) == 0);
    TEST_CHECK(lowSunShadowLevels(-3.0, 0.0, kLowSunShadowLevels) == 0);
    TEST_CHECK(lowSunShadowLevels(5.0, -1.0, 0) == 0);
}

// The sun from 20 degrees down to 3 and back, wavering by 0.4 degrees: one switch each way.
void testNoBlinking() {
    int levels = 0;
    int switches = 0;
    for (int step = 0; step <= 680; ++step) {
        const double sweep = step <= 340 ? 20.0 - 0.05 * step : 3.0 + 0.05 * (step - 340);
        const double sun = sweep + 0.4 * std::sin(step * 0.9);
        const int next = lowSunShadowLevels(sun, 11.0, levels);
        switches += next != levels ? 1 : 0;
        levels = next;
    }
    TEST_CHECK(switches == 2 && levels == 0);
}

} // namespace

int main() {
    testElevation();
    testRule();
    testNoBlinking();
    return test::result();
}

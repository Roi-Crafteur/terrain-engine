// The scores of the settlement (generation/settlement/preference.hpp): the trapezoid of section 6
// of docs/PEUPLEMENT.md with its soft floor and hard limits, the ramps, the physical score.

#include "terrain/generation/settlement/preference.hpp"

#include "test_assert.hpp"

#include <cmath>
#include <cstdio>

namespace st = terrain::settlement;

namespace {

bool near(double a, double b) {
    return std::fabs(a - b) < 1e-12;
}

// The example of the document: 500 / 1,500 / 4,000, floor 0.05.
void testDocumentExample() {
    st::Trapezoid t;
    t.min = 500.0;
    t.idealMin = 1500.0;
    t.idealMax = 1500.0;
    t.max = 4000.0;
    TEST_CHECK(near(st::trapezoidScore(0.0, t), 0.05));
    TEST_CHECK(near(st::trapezoidScore(1000.0, t), 0.525));
    TEST_CHECK(near(st::trapezoidScore(1500.0, t), 1.0));
    TEST_CHECK(near(st::trapezoidScore(2750.0, t), 0.525));
    TEST_CHECK(near(st::trapezoidScore(6000.0, t), 0.05));
}

void testPlateauAndLimits() {
    st::Trapezoid t;
    t.hardMin = 0.0;
    t.min = 500.0;
    t.idealMin = 1000.0;
    t.idealMax = 2000.0;
    t.max = 4000.0;
    t.hardMax = 7000.0;
    TEST_CHECK(st::trapezoidScore(-1.0, t) == 0.0);
    TEST_CHECK(near(st::trapezoidScore(0.0, t), 0.05));
    TEST_CHECK(near(st::trapezoidScore(250.0, t), 0.05));
    TEST_CHECK(st::trapezoidScore(1000.0, t) == 1.0);
    TEST_CHECK(st::trapezoidScore(1700.0, t) == 1.0);
    TEST_CHECK(st::trapezoidScore(2000.0, t) == 1.0);
    TEST_CHECK(near(st::trapezoidScore(3000.0, t), 0.525));
    TEST_CHECK(near(st::trapezoidScore(6999.0, t), 0.05));
    TEST_CHECK(st::trapezoidScore(7001.0, t) == 0.0);
    // The distance to the water: the bank is ideal (idealMin = min = 0).
    st::Trapezoid water;
    water.min = 0.0;
    water.idealMin = 0.0;
    water.idealMax = 15.0;
    water.max = 100.0;
    TEST_CHECK(st::trapezoidScore(0.0, water) == 1.0);
    TEST_CHECK(st::trapezoidScore(15.0, water) == 1.0);
    TEST_CHECK(st::trapezoidScore(57.5, water) > 0.5 && st::trapezoidScore(57.5, water) < 0.55);
    TEST_CHECK(near(st::trapezoidScore(150.0, water), 0.05));
}

void testRamps() {
    TEST_CHECK(st::rampScore(3000.0, st::Ramp{3500.0, 6500.0}) == 0.0);
    TEST_CHECK(near(st::rampScore(5000.0, st::Ramp{3500.0, 6500.0}), 0.5));
    TEST_CHECK(st::rampScore(9000.0, st::Ramp{3500.0, 6500.0}) == 1.0);
    // Decreasing: the cold from -4 to -20 degrees.
    TEST_CHECK(st::rampScore(0.0, st::Ramp{-4.0, -20.0}) == 0.0);
    TEST_CHECK(near(st::rampScore(-12.0, st::Ramp{-4.0, -20.0}), 0.5));
    TEST_CHECK(st::rampScore(-30.0, st::Ramp{-4.0, -20.0}) == 1.0);
}

void testPhysicalScore() {
    const st::PhysicalScoreRule rule;
    st::Preferences p;
    st::CriterionValues values{};
    TEST_CHECK(st::physicalScore(p, values, rule) == 1.0);
    const auto altitude = static_cast<std::size_t>(st::Criterion::Altitude);
    const auto temperature = static_cast<std::size_t>(st::Criterion::Temperature);
    p.has[altitude] = true;
    p.range[altitude] = st::Trapezoid{-st::kInfinity, 0.0, 100.0, 1000.0, 2000.0, 5000.0, 0.05};
    p.has[temperature] = true;
    p.range[temperature] = st::Trapezoid{-st::kInfinity, -10.0, 5.0, 20.0, 30.0, st::kInfinity,
                                         0.05};
    p.weight[temperature] = 3.0;
    values[altitude] = 500.0;    // 1
    values[temperature] = 40.0;  // the floor
    // 0.7 x (1 x 1 + 3 x 0.05) / 4 + 0.3 x 0.05
    TEST_CHECK(near(st::physicalScore(p, values, rule), 0.7 * 1.15 / 4.0 + 0.3 * 0.05));
    values[altitude] = 6000.0; // beyond hardMax: forbidden
    TEST_CHECK(st::physicalScore(p, values, rule) == 0.0);
    TEST_CHECK(st::criterionNamed("waterDistance") == st::Criterion::WaterDistance);
    TEST_CHECK(!st::criterionNamed("slope"));
    TEST_CHECK(st::criterionName(st::Criterion::CoastDistance) == "coastDistance");
}

void testQuantize() {
    TEST_CHECK(st::quantize(0.0) == 0 && st::quantize(1.0) == 255);
    TEST_CHECK(st::quantize(-3.0) == 0 && st::quantize(7.0) == 255);
    TEST_CHECK(st::quantize(0.5) == 128);
    TEST_CHECK(st::unquantize(255) == 1.0);
}

} // namespace

int main() {
    testDocumentExample();
    testPlateauAndLimits();
    testRamps();
    testPhysicalScore();
    testQuantize();
    return test::result();
}

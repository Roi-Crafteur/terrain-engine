#include "terrain/generation/sdf.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

namespace sdf = terrain::sdf;

constexpr std::array<float, 6> kValues = {-30.0f, -2.5f, -0.25f, 0.0f, 0.5f, 7.0f};

// Grid of distances in [-5, 5] with a step of 0.25, exact in a float.
constexpr int kGridHalf = 20;
constexpr float gridValue(int i) {
    return static_cast<float>(i) * 0.25f;
}

// Union, intersection and difference: sign logic on a few shapes.
void testBooleanSigns() {
    // Negative is inside, positive is outside.
    TEST_CHECK(sdf::unite(-1.0f, 2.0f) < 0.0f);   // inside one of them
    TEST_CHECK(sdf::unite(-1.0f, -2.0f) < 0.0f);
    TEST_CHECK(sdf::unite(2.0f, 3.0f) > 0.0f);    // outside both

    TEST_CHECK(sdf::intersect(-1.0f, -2.0f) < 0.0f); // inside both
    TEST_CHECK(sdf::intersect(-1.0f, 2.0f) > 0.0f);
    TEST_CHECK(sdf::intersect(2.0f, 3.0f) > 0.0f);

    TEST_CHECK(sdf::subtract(-1.0f, 2.0f) < 0.0f);  // in a, outside b: kept
    TEST_CHECK(sdf::subtract(-1.0f, -2.0f) > 0.0f); // in a and in b: carved out
    TEST_CHECK(sdf::subtract(2.0f, 3.0f) > 0.0f);   // outside a
    TEST_CHECK(sdf::subtract(2.0f, -3.0f) > 0.0f);

    // The surface of a shape is kept where the other shape does not reach it.
    TEST_CHECK(sdf::unite(0.0f, 5.0f) == 0.0f);
    TEST_CHECK(sdf::intersect(0.0f, -5.0f) == 0.0f);
    TEST_CHECK(sdf::subtract(0.0f, 5.0f) == 0.0f);
}

// Exact min / max values, and the algebra of the operations.
void testBooleanValues() {
    for (const float a : kValues) {
        TEST_CHECK(sdf::unite(a, a) == a);
        TEST_CHECK(sdf::intersect(a, a) == a);
        for (const float b : kValues) {
            TEST_CHECK(sdf::unite(a, b) == std::min(a, b));
            TEST_CHECK(sdf::intersect(a, b) == std::max(a, b));
            TEST_CHECK(sdf::subtract(a, b) == std::max(a, -b));
            TEST_CHECK(sdf::unite(a, b) == sdf::unite(b, a));
            TEST_CHECK(sdf::intersect(a, b) == sdf::intersect(b, a));
            // A difference is an intersection with the complement of b.
            TEST_CHECK(sdf::subtract(a, b) == sdf::intersect(a, -b));
        }
    }
    TEST_CHECK(sdf::unite(-3.0f, 4.0f) == -3.0f);
    TEST_CHECK(sdf::intersect(-3.0f, 4.0f) == 4.0f);
    TEST_CHECK(sdf::subtract(-3.0f, -4.0f) == 4.0f);
    TEST_CHECK(sdf::subtract(-3.0f, 4.0f) == -3.0f);
    // Subtraction is not symmetric.
    TEST_CHECK(sdf::subtract(-3.0f, -4.0f) != sdf::subtract(-4.0f, -3.0f));
}

// Inside the footprint and below the top the prism is rock, anywhere else it is air.
void testPrismSigns() {
    constexpr double kTop = 300.0;
    TEST_CHECK(sdf::prismBelow(-50.0f, kTop - 10.0, kTop) < 0.0f);
    TEST_CHECK(sdf::prismBelow(-50.0f, kTop - 1000.0, kTop) < 0.0f);
    TEST_CHECK(sdf::prismBelow(-50.0f, kTop + 3.0, kTop) > 0.0f);  // above the top
    TEST_CHECK(sdf::prismBelow(20.0f, kTop - 100.0, kTop) > 0.0f); // outside the footprint
    TEST_CHECK(sdf::prismBelow(20.0f, kTop + 100.0, kTop) > 0.0f);
    TEST_CHECK(sdf::prismBelow(-0.5f, kTop - 0.25, kTop) < 0.0f); // just inside both
}

// Deep inside the footprint the top is flat: the value is the signed height above it.
void testPrismFlatTop() {
    constexpr double kTop = 123.5;
    for (const double offset : {-60.0, -7.5, -0.125, 0.0, 0.125, 12.25, 90.0}) {
        const double y = kTop + offset;
        TEST_CHECK(sdf::prismBelow(-1000.0f, y, kTop) == static_cast<float>(y - kTop));
    }
    TEST_CHECK(sdf::prismBelow(-1000.0f, kTop, kTop) == 0.0f);
    TEST_CHECK(sdf::prismBelow(-10.0f, kTop, kTop) == 0.0f); // the top plane is the surface
}

// Well below the top the side is vertical: the value is the footprint, whatever y is.
void testPrismVerticalSide() {
    constexpr double kTop = -80.0;
    for (const float footprint : {-20.0f, -2.0f, 0.0f, 0.5f, 3.0f, 40.0f}) {
        for (const double depth : {1000.0, 500.0, 100.0, 50.0}) {
            TEST_CHECK(sdf::prismBelow(footprint, kTop - depth, kTop) == footprint);
        }
    }
}

// Above the floor and within the half-width the slot is a volume (negative).
void testSlotSigns() {
    constexpr double kFloor = 40.0;
    TEST_CHECK(sdf::slotAbove(3.0f, 10.0f, kFloor + 25.0, kFloor) < 0.0f);  // inside the slot
    TEST_CHECK(sdf::slotAbove(0.0f, 10.0f, kFloor + 1000.0, kFloor) < 0.0f); // the slot is open
    TEST_CHECK(sdf::slotAbove(3.0f, 10.0f, kFloor - 5.0, kFloor) > 0.0f);   // below the floor
    TEST_CHECK(sdf::slotAbove(30.0f, 10.0f, kFloor + 25.0, kFloor) > 0.0f); // beyond the wall
    TEST_CHECK(sdf::slotAbove(30.0f, 10.0f, kFloor - 25.0, kFloor) > 0.0f);

    // Carving the slot out of rock leaves air inside it and rock around it.
    const float rock = -10.0f;
    TEST_CHECK(sdf::subtract(rock, sdf::slotAbove(3.0f, 10.0f, kFloor + 25.0, kFloor)) > 0.0f);
    TEST_CHECK(sdf::subtract(rock, sdf::slotAbove(30.0f, 10.0f, kFloor + 25.0, kFloor)) < 0.0f);
    TEST_CHECK(sdf::subtract(rock, sdf::slotAbove(3.0f, 10.0f, kFloor - 5.0, kFloor)) < 0.0f);
}

// Outside the half-width the walls are vertical: the value does not depend on y.
void testSlotVerticalWalls() {
    constexpr double kFloor = -15.0;
    for (const double height : {1.0, 10.0, 100.0, 1000.0}) {
        TEST_CHECK(sdf::slotAbove(30.0f, 10.0f, kFloor + height, kFloor) == 20.0f);
        TEST_CHECK(sdf::slotAbove(12.5f, 10.0f, kFloor + height, kFloor) == 2.5f);
    }
    // The wall itself: zero on the half-width line.
    TEST_CHECK(sdf::slotAbove(10.0f, 10.0f, kFloor + 50.0, kFloor) == 0.0f);
}

// Well inside the half-width the floor is flat: the value is the signed depth below it.
void testSlotFlatFloor() {
    constexpr double kFloor = 77.25;
    for (const double offset : {-30.0, -7.0, -0.5, 0.0, 0.5, 3.5, 20.0}) {
        const double y = kFloor + offset;
        TEST_CHECK(sdf::slotAbove(0.0f, 1000.0f, y, kFloor) == static_cast<float>(kFloor - y));
    }
    TEST_CHECK(sdf::slotAbove(0.0f, 1000.0f, kFloor, kFloor) == 0.0f);
}

// Inside the radius and between the two heights the cylinder is rock; its side is vertical.
void testCylinder() {
    constexpr double kBottom = 100.0;
    constexpr double kTop = 112.0;
    TEST_CHECK(sdf::verticalCylinder(2.0f, 6.0f, 106.0, kBottom, kTop) < 0.0f);
    TEST_CHECK(sdf::verticalCylinder(2.0f, 6.0f, 113.0, kBottom, kTop) > 0.0f); // above the top
    TEST_CHECK(sdf::verticalCylinder(2.0f, 6.0f, 99.0, kBottom, kTop) > 0.0f);  // below the base
    TEST_CHECK(sdf::verticalCylinder(7.0f, 6.0f, 106.0, kBottom, kTop) > 0.0f); // beside it
    TEST_CHECK(sdf::verticalCylinder(6.0f, 6.0f, 106.0, kBottom, kTop) == 0.0f);
    for (const double y : {101.0, 104.0, 108.5, 111.0}) {
        TEST_CHECK(sdf::verticalCylinder(9.0f, 6.0f, y, kBottom, kTop) == 3.0f);
    }
    TEST_CHECK(sdf::verticalCylinder(0.0f, 6.0f, kTop, kBottom, kTop) == 0.0f); // flat top
}

// The torus is the set of points within the tube radius of its central circle.
void testTorus() {
    TEST_CHECK(sdf::torus(20.0f, 0.0f, 20.0f, 5.0f) == -5.0f); // on the central circle
    TEST_CHECK(sdf::torus(25.0f, 0.0f, 20.0f, 5.0f) == 0.0f);  // outer equator
    TEST_CHECK(sdf::torus(15.0f, 0.0f, 20.0f, 5.0f) == 0.0f);  // inner equator
    TEST_CHECK(sdf::torus(20.0f, 5.0f, 20.0f, 5.0f) == 0.0f);  // top of the tube
    TEST_CHECK(sdf::torus(0.0f, 0.0f, 20.0f, 5.0f) == 15.0f);  // the hole
    TEST_CHECK(sdf::torus(23.0f, 4.0f, 20.0f, 5.0f) == 0.0f);  // 3-4-5 triangle
    TEST_CHECK(sdf::torus(20.0f, -12.0f, 20.0f, 5.0f) == 7.0f); // symmetric along the axis
}

// Outside the blend band, and for a radius of zero or less, the smooth operators are the hard ones.
void testSmoothExact() {
    constexpr float kRadius = 2.0f;
    for (const float a : kValues) {
        for (const float b : kValues) {
            for (const float k : {0.0f, -1.0f}) {
                TEST_CHECK(sdf::smoothMax(a, b, k) == std::max(a, b));
                TEST_CHECK(sdf::smoothMin(a, b, k) == std::min(a, b));
            }
            if (std::abs(a - b) >= kRadius) {
                TEST_CHECK(sdf::smoothMax(a, b, kRadius) == std::max(a, b));
                TEST_CHECK(sdf::smoothMin(a, b, kRadius) == std::min(a, b));
            }
        }
    }
    // The band is closed on the outside: a gap of exactly k is already the hard operator.
    TEST_CHECK(sdf::smoothMax(0.0f, 2.0f, 2.0f) == 2.0f);
    TEST_CHECK(sdf::smoothMax(2.0f, 0.0f, 2.0f) == 2.0f);
    TEST_CHECK(sdf::smoothMin(0.0f, 2.0f, 2.0f) == 0.0f);
    TEST_CHECK(sdf::smoothMin(2.0f, 0.0f, 2.0f) == 0.0f);
    // Two equal values are the worst case: the blend adds the full k / 4.
    TEST_CHECK(sdf::smoothMax(1.0f, 1.0f, 2.0f) == 1.5f);
    TEST_CHECK(sdf::smoothMin(1.0f, 1.0f, 2.0f) == 0.5f);
    TEST_CHECK(sdf::smoothMax(-3.0f, -3.0f, 8.0f) == -1.0f);
}

// The blend stays within k / 4 of the hard operator, on the soft side of it.
void testSmoothBounds() {
    constexpr float kRadius = 2.0f;
    for (int i = -kGridHalf; i <= kGridHalf; ++i) {
        for (int j = -kGridHalf; j <= kGridHalf; ++j) {
            const float a = gridValue(i);
            const float b = gridValue(j);
            const float hardMax = std::max(a, b);
            const float hardMin = std::min(a, b);
            TEST_CHECK(sdf::smoothMax(a, b, kRadius) >= hardMax);
            TEST_CHECK(sdf::smoothMax(a, b, kRadius) <= hardMax + kRadius / 4.0f);
            TEST_CHECK(sdf::smoothMin(a, b, kRadius) <= hardMin);
            TEST_CHECK(sdf::smoothMin(a, b, kRadius) >= hardMin - kRadius / 4.0f);
            // The minimum is the maximum seen in a mirror.
            TEST_CHECK(sdf::smoothMin(a, b, kRadius) == -sdf::smoothMax(-a, -b, kRadius));
        }
    }
}

// A small step of one argument moves the result by at most that step (slope <= 1).
void testSmoothSlope() {
    constexpr float kRadius = 2.0f;
    constexpr float kStep = 0.03125f; // 1/32: grid values plus a step stay exact
    // Slack for the float rounding of the two results, a few ulps for values below 8.
    constexpr float kSlack = 2.0e-6f;
    constexpr float kLimit = kStep * (1.0f + 1.0e-4f) + kSlack;
    bool bounded = true;
    for (int i = -kGridHalf; i <= kGridHalf; ++i) {
        for (int j = -kGridHalf; j <= kGridHalf; ++j) {
            const float a = gridValue(i);
            const float b = gridValue(j);
            const float max0 = sdf::smoothMax(a, b, kRadius);
            const float min0 = sdf::smoothMin(a, b, kRadius);
            bounded = bounded && std::abs(sdf::smoothMax(a + kStep, b, kRadius) - max0) <= kLimit &&
                      std::abs(sdf::smoothMax(a, b + kStep, kRadius) - max0) <= kLimit &&
                      std::abs(sdf::smoothMin(a + kStep, b, kRadius) - min0) <= kLimit &&
                      std::abs(sdf::smoothMin(a, b + kStep, kRadius) - min0) <= kLimit;
        }
    }
    TEST_CHECK(bounded);
}

// A smooth subtraction is the smooth maximum with the complement, and never adds rock to a.
void testSmoothSubtract() {
    for (const float k : {0.0f, 0.5f, 2.0f, 10.0f}) {
        for (const float a : kValues) {
            for (const float b : kValues) {
                TEST_CHECK(sdf::smoothSubtract(a, b, k) == sdf::smoothMax(a, -b, k));
                TEST_CHECK(sdf::smoothSubtract(a, b, k) >= a);
                TEST_CHECK(sdf::smoothSubtract(a, b, k) >= sdf::subtract(a, b));
            }
        }
    }
    for (const float a : kValues) {
        for (const float b : kValues) {
            TEST_CHECK(sdf::smoothSubtract(a, b, 0.0f) == sdf::subtract(a, b));
        }
    }
    // Where the two surfaces cross, the seam is rounded off into the air.
    TEST_CHECK(sdf::smoothSubtract(0.0f, 0.0f, 2.0f) == 0.5f);
    // Far from the carved shape the rock is untouched, deep inside it the rock is gone.
    TEST_CHECK(sdf::smoothSubtract(-4.0f, 9.0f, 2.0f) == -4.0f);
    TEST_CHECK(sdf::smoothSubtract(-4.0f, -9.0f, 2.0f) == 9.0f);
}

// A smooth union is the smooth minimum: never less solid than either shape, and symmetric.
void testSmoothUnite() {
    for (const float k : {0.0f, 0.5f, 2.0f, 10.0f}) {
        for (const float a : kValues) {
            for (const float b : kValues) {
                const float united = sdf::smoothUnite(a, b, k);
                TEST_CHECK(united == sdf::smoothMin(a, b, k));
                TEST_CHECK(united <= std::min(a, b));
                TEST_CHECK(united >= std::min(a, b) - k / 4.0f);
                TEST_CHECK(united == sdf::smoothUnite(b, a, k));
                TEST_CHECK(sdf::smoothMax(a, b, k) == sdf::smoothMax(b, a, k));
            }
        }
    }
    for (const float a : kValues) {
        for (const float b : kValues) {
            TEST_CHECK(sdf::smoothUnite(a, b, 0.0f) == sdf::unite(a, b));
        }
    }
    // Two shapes touching at their surfaces are joined by a fillet filled with rock.
    TEST_CHECK(sdf::smoothUnite(0.0f, 0.0f, 2.0f) == -0.5f);
    TEST_CHECK(sdf::smoothUnite(-4.0f, 9.0f, 2.0f) == -4.0f);
}

} // namespace

int main() {
    testBooleanSigns();
    testBooleanValues();
    testPrismSigns();
    testPrismFlatTop();
    testPrismVerticalSide();
    testSlotSigns();
    testSlotVerticalWalls();
    testSlotFlatFloor();
    testCylinder();
    testTorus();
    testSmoothExact();
    testSmoothBounds();
    testSmoothSlope();
    testSmoothSubtract();
    testSmoothUnite();
    return test::result();
}

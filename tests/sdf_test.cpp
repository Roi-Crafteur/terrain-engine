#include "terrain/generation/sdf.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>

namespace {

namespace sdf = terrain::sdf;

constexpr std::array<float, 6> kValues = {-30.0f, -2.5f, -0.25f, 0.0f, 0.5f, 7.0f};

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
    return test::result();
}

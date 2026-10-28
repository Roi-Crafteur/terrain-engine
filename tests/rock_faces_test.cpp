// The faces of the rock in three dimensions (phase 12A): without a face the density is y - ground
// bit for bit; within the heights of a face it is the horizontal distance to it, with its cap
// jutting over the air and its undercut receding into the rock; the faces of a valley shape the
// air its river carves; the levees fill; the coarse detail leaves the small features out; the
// bounds and the share of rock follow the faces.
#include "terrain/generation/rock_faces.hpp"

#include "test_assert.hpp"

#include <cmath>

namespace {

using terrain::RockColumn;
using terrain::RockFace;

RockColumn columnOf(float ground, float uncarved) {
    RockColumn c;
    c.ground = ground;
    c.uncarved = uncarved;
    return c;
}

RockFace faceOf(float foot, float top, float distance) {
    RockFace f;
    f.foot = foot;
    f.top = top;
    f.distance = distance;
    return f;
}

void testWithoutFace() {
    const RockColumn c = columnOf(12.375f, 40.0f);
    bool exact = true;
    for (const double y : {-3.0, 0.0, 12.0, 12.375, 13.5, 1000.25}) {
        exact = exact && terrain::rockDensity(c, y, {}) == static_cast<float>(y - c.ground);
    }
    TEST_CHECK(exact);
    const terrain::RockBounds b = terrain::rockBounds(c);
    TEST_CHECK(b.low == 12.375f && b.high == 40.0f);
    TEST_CHECK(terrain::rockShare(c, 12.0) == 0.0f);
}

// A face from 0 to 20 m: the column 3 m in front of it is air between its foot and its top, rock
// below its foot; the column 3 m behind it is rock up to its top.
void testVerticalFace() {
    RockColumn front = columnOf(10.0f, 10.0f); // on the 2.5D riser, before its middle
    front.add(faceOf(0.0f, 20.0f, 3.0f));
    TEST_CHECK(terrain::rockDensity(front, 15.0, {}) == 3.0f);
    TEST_CHECK(terrain::rockDensity(front, 5.0, {}) == 3.0f); // lowered from 10 m to its foot
    TEST_CHECK(terrain::rockDensity(front, -1.0, {}) < 0.0f);
    RockColumn behind = columnOf(15.0f, 15.0f);
    behind.add(faceOf(0.0f, 20.0f, -3.0f));
    TEST_CHECK(terrain::rockDensity(behind, 18.0, {}) == -3.0f); // raised from 15 m to its top
    TEST_CHECK(terrain::rockDensity(behind, 21.0, {}) > 0.0f);
    // Continuous across the face: the density changes with the distance, not with y.
    TEST_CHECK(terrain::rockDensity(behind, 2.0, {}) == terrain::rockDensity(behind, 19.0, {}));
}

void testCapAndUndercut() {
    RockFace f = faceOf(0.0f, 20.0f, 2.0f);
    f.cap = 3.0f;
    f.capDepth = 5.0f;
    f.undercut = 3.5f;
    f.undercutHeight = 3.0f;
    RockColumn front = columnOf(0.0f, 0.0f);
    front.add(f);
    // Under the lip, within 2 m of the face: rock (3 m of cap), above the floor: air.
    TEST_CHECK(terrain::rockDensity(front, 19.5, {}) < 0.0f);
    TEST_CHECK(terrain::rockDensity(front, 10.0, {}) > 0.0f);
    // The cap rises from nothing at capDepth below the top: a ceiling sloping up to the lip.
    TEST_CHECK(terrain::rockDensity(front, 15.2, {}) > terrain::rockDensity(front, 18.0, {}));
    f.distance = -2.0f; // 2 m into the rock
    RockColumn behind = columnOf(20.0f, 20.0f);
    behind.add(f);
    TEST_CHECK(terrain::rockDensity(behind, 0.5, {}) > 0.0f); // in the niche
    TEST_CHECK(terrain::rockDensity(behind, 2.9, {}) < 0.0f); // its arch closes
    TEST_CHECK(terrain::rockDensity(behind, 10.0, {}) < 0.0f);
    // The coarse detail leaves out the caps and the undercuts narrower than 4 m.
    const terrain::RockDetail coarse = terrain::rockDetailForLod(3);
    TEST_CHECK(terrain::rockDensity(behind, 0.5, coarse) == -2.0f);
    TEST_CHECK(terrain::rockDetailForLod(0).leastFeature == 0.0f);
    TEST_CHECK(terrain::rockDetailForLod(2) == terrain::rockDetailForLod(0));
}

// A wall of a slot: the river carves the relief from 50 m down to 10 m; in front of its face the
// air goes down to the floor, behind it the rock stays.
void testCarvedFace() {
    RockFace wall = faceOf(10.0f, 30.0f, -2.0f);
    wall.carved = true;
    RockColumn behind = columnOf(10.0f, 50.0f);
    behind.add(wall);
    TEST_CHECK(terrain::rockDensity(behind, 20.0, {}) == -2.0f);
    TEST_CHECK(terrain::rockDensity(behind, 40.0, {}) > 0.0f); // above the face: carved
    TEST_CHECK(terrain::rockDensity(behind, 60.0, {}) > 0.0f);
    wall.distance = 2.0f;
    RockColumn front = columnOf(10.0f, 50.0f);
    front.add(wall);
    TEST_CHECK(terrain::rockDensity(front, 20.0, {}) == 2.0f);
    TEST_CHECK(terrain::rockDensity(front, 9.0, {}) < 0.0f);
}

// A levee fills the relief up to the ground, even in front of a face of the relief.
void testLevee() {
    RockColumn c = columnOf(14.0f, 12.0f);
    c.add(faceOf(0.0f, 30.0f, 5.0f));
    TEST_CHECK(terrain::rockDensity(c, 13.0, {}) < 0.0f);
    TEST_CHECK(terrain::rockDensity(c, 15.0, {}) > 0.0f);
}

void testBoundsShareAndAdd() {
    RockColumn c = columnOf(10.0f, 10.0f);
    c.add(faceOf(-5.0f, 30.0f, 1.0f));
    const terrain::RockBounds b = terrain::rockBounds(c);
    TEST_CHECK(b.low == -5.0f && b.high == 30.0f);
    TEST_CHECK(terrain::rockShare(c, 12.0) == 1.0f);
    TEST_CHECK(terrain::rockShare(c, 40.0) == 0.0f);
    c.faces[0].distance = 50.0f;
    TEST_CHECK(terrain::rockShare(c, 12.0) == 0.0f);
    // Past kMaxRockFaces, the nearest are kept.
    RockColumn full = columnOf(0.0f, 0.0f);
    for (const float d : {9.0f, -8.0f, 7.0f, 6.0f, -1.0f, 30.0f}) {
        full.add(faceOf(0.0f, 1.0f, d));
    }
    TEST_CHECK(full.faceCount == terrain::kMaxRockFaces);
    bool nearest = true;
    for (int i = 0; i < full.faceCount; ++i) {
        nearest = nearest && std::abs(full.faces[static_cast<std::size_t>(i)].distance) < 8.5f;
    }
    TEST_CHECK(nearest);
}

} // namespace

int main() {
    testWithoutFace();
    testVerticalFace();
    testCapAndUndercut();
    testCarvedFace();
    testLevee();
    testBoundsShareAndAdd();
    return test::result();
}

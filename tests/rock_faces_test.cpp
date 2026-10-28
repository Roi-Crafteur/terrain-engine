// The faces of the rock in three dimensions (phase 12A): without a face the density is y - ground
// bit for bit; within the run of a face the climb of the relief becomes a step, a vertical face at
// its middle between the foot and the top, lifted by what the relief differs from the line of the
// run, and meeting the relief at the edges of the run; its cap juts over the air and its undercut
// recedes into the rock; its grain moves it by bands; the faces of a valley shape the floor its
// river carves; the levees fill; a coarse detail fades the narrow runs, overhangs and grain out;
// the bounds and the share of rock follow the faces; anchoring scales the overhangs.
#include "terrain/generation/rock_faces.hpp"

#include "test_assert.hpp"

#include <cmath>

namespace {

using terrain::RockColumn;
using terrain::RockFace;

constexpr float kFoot = 0.0f;
constexpr float kTop = 20.0f;
constexpr float kRun = 10.0f;

// The relief on the line of the run of the face below, distance meters in front of its middle.
float rampAt(float distance) {
    return 0.5f * (kFoot + kTop) - (kTop - kFoot) * distance / kRun;
}

RockFace faceAt(float distance) {
    RockFace f;
    f.foot = kFoot;
    f.top = kTop;
    f.distance = distance;
    f.run = kRun;
    return f;
}

// A column distance meters in front of the face, its relief lift meters above the line of the run.
RockColumn columnAt(float distance, float lift = 0.0f) {
    RockColumn c;
    c.ground = rampAt(distance) + lift;
    c.uncarved = c.ground;
    c.add(faceAt(distance));
    return c;
}

bool near(double a, double b, double tolerance) {
    return std::abs(a - b) <= tolerance;
}

// The height of the top of the rock of a column (scanned down from above it).
double topOf(const RockColumn& c, const terrain::RockDetail& detail = {}) {
    for (double y = 60.0; y > -60.0; y -= 0.01) {
        if (terrain::rockDensity(c, y, detail) < 0.0f) {
            return y;
        }
    }
    return -60.0;
}

void testWithoutFace() {
    RockColumn c;
    c.ground = 12.375f;
    c.uncarved = 40.0f;
    bool exact = true;
    for (const double y : {-3.0, 0.0, 12.0, 12.375, 13.5, 1000.25}) {
        exact = exact && terrain::rockDensity(c, y, {}) == static_cast<float>(y - c.ground);
    }
    TEST_CHECK(exact);
    const terrain::RockBounds b = terrain::rockBounds(c);
    TEST_CHECK(b.low == 12.375f && b.high == 40.0f);
    TEST_CHECK(terrain::rockShare(c, 12.0) == 0.0f);
}

// Behind the face the rock rises to its top, in front of it the air goes down to its foot; across
// the face, at mid height, the density is the distance to it.
void testStep() {
    const RockColumn behind = columnAt(-2.0f);
    const RockColumn front = columnAt(2.0f);
    TEST_CHECK(near(topOf(behind), kTop, 0.6));
    TEST_CHECK(near(topOf(front), kFoot, 0.6));
    TEST_CHECK(near(terrain::rockDensity(behind, 10.0, {}), -2.0, 1e-3));
    TEST_CHECK(near(terrain::rockDensity(front, 10.0, {}), 2.0, 1e-3));
    // The relief 3 m above the line of the run lifts the step by 3 m.
    TEST_CHECK(near(topOf(columnAt(-2.0f, 3.0f)), kTop + 3.0, 0.6));
    // At the edges of the run the step meets the relief.
    TEST_CHECK(near(topOf(columnAt(-0.5f * kRun)), rampAt(-0.5f * kRun), 0.05));
    TEST_CHECK(near(topOf(columnAt(0.5f * kRun)), rampAt(0.5f * kRun), 0.05));
}

void testCapAndUndercut() {
    RockFace f = faceAt(2.0f);
    f.cap = 4.0f;
    f.capDepth = 6.0f;
    f.undercut = 4.0f;
    f.undercutHeight = 5.0f;
    RockColumn front;
    front.ground = rampAt(2.0f);
    front.uncarved = front.ground;
    front.add(f);
    // Under the lip, 2 m in front of the face: rock (4 m of cap), above the floor: air.
    TEST_CHECK(terrain::rockDensity(front, 19.0, {}) < 0.0f);
    TEST_CHECK(terrain::rockDensity(front, 10.0, {}) > 0.0f);
    // The cap rises from nothing capDepth below the top: a ceiling sloping up to the lip.
    TEST_CHECK(terrain::rockDensity(front, 14.5, {}) > terrain::rockDensity(front, 18.0, {}));
    f.distance = -2.0f; // 2 m into the rock
    RockColumn behind;
    behind.ground = rampAt(-2.0f);
    behind.uncarved = behind.ground;
    behind.add(f);
    TEST_CHECK(terrain::rockDensity(behind, 1.0, {}) > 0.0f);  // in the niche
    TEST_CHECK(terrain::rockDensity(behind, 4.9, {}) < 0.0f);  // its arch closes
    TEST_CHECK(terrain::rockDensity(behind, 10.0, {}) < 0.0f);
    // Cells of 2 m draw them in part (4 m is 2 cells), of 4 m not at all (1 cell).
    TEST_CHECK(terrain::rockDensity(behind, 1.0, terrain::rockDetailForLod(2)) < 0.0f);
    TEST_CHECK(terrain::rockDensity(front, 19.0, terrain::rockDetailForLod(2)) > 0.0f);
    TEST_CHECK(terrain::rockDensity(behind, 1.0, terrain::rockDetailForLod(1)) <
               terrain::rockDensity(behind, 1.0, {}));
    TEST_CHECK(terrain::rockDetailForLod(0).cell == 1.0f &&
               terrain::rockDetailForLod(3).cell == 8.0f);
}

// A run of less than 3 cells fades out (the columns would alternate in and out of it), weights
// below 1 blend the step with the relief.
void testFades() {
    RockColumn narrow = columnAt(-1.0f);
    narrow.faces[0].run = 2.0f;
    for (const double y : {-4.0, 3.5, 11.25}) {
        TEST_CHECK(terrain::rockDensity(narrow, y, {}) == static_cast<float>(y - narrow.ground));
    }
    RockColumn half = columnAt(-2.0f);
    half.faces[0].weight = 0.5f;
    const double full = terrain::rockDensity(columnAt(-2.0f), 16.0, {});
    const double relief = 16.0 - static_cast<double>(half.ground);
    TEST_CHECK(near(terrain::rockDensity(half, 16.0, {}), 0.5 * (full + relief), 1e-4));
}

// The grain moves the face by bands up to twice its amplitude; cells of 8 m leave it out.
void testGrain() {
    // A face of 100 m over a run of 40 m, the column 0.5 m behind it on the line of the run.
    RockFace f;
    f.foot = 0.0f;
    f.top = 100.0f;
    f.distance = -0.5f;
    f.run = 40.0f;
    f.grain = 1.0f;
    f.grainShift = 3.0f;
    f.grainAlong = 0.5f;
    RockColumn c;
    c.ground = 50.0f + 100.0f * 0.5f / 40.0f;
    c.uncarved = c.ground;
    c.add(f);
    double least = 1e9;
    double most = -1e9;
    // Between the rounded edges of the step.
    for (double y = 10.0; y < 90.0; y += 0.25) {
        const double d = terrain::rockDensity(c, y, {});
        least = std::min(least, d);
        most = std::max(most, d);
    }
    TEST_CHECK(most - least > 0.5 && least > -0.5 - 2.1 && most < -0.5 + 2.1);
    TEST_CHECK(near(terrain::rockDensity(c, 50.0, terrain::rockDetailForLod(3)), -0.5, 1e-3));
}

// The faces of a slot: the river carves the relief from 40 m down to its floor, which the face of
// the valley steps; in front of the face the air goes down to its foot.
void testCarvedFace() {
    RockFace wall = faceAt(-2.0f);
    wall.carved = true;
    RockColumn behind;
    behind.ground = rampAt(-2.0f);
    behind.uncarved = 40.0f;
    behind.add(wall);
    TEST_CHECK(terrain::rockDensity(behind, 15.0, {}) < 0.0f);
    TEST_CHECK(terrain::rockDensity(behind, 30.0, {}) > 0.0f); // above the step: carved
    TEST_CHECK(terrain::rockDensity(behind, 41.0, {}) > 0.0f);
    wall.distance = 2.0f;
    RockColumn front;
    front.ground = rampAt(2.0f);
    front.uncarved = 40.0f;
    front.add(wall);
    TEST_CHECK(terrain::rockDensity(front, 5.0, {}) > 0.0f);
    TEST_CHECK(terrain::rockDensity(front, -1.0, {}) < 0.0f);
}

// A levee fills the relief up to the ground, even in front of a face of the relief.
void testLevee() {
    RockColumn c = columnAt(3.0f);
    c.ground = c.uncarved + 2.0f;
    TEST_CHECK(terrain::rockDensity(c, c.ground - 0.5, {}) < 0.0f);
    TEST_CHECK(terrain::rockDensity(c, c.ground + 0.5, {}) > 0.0f);
}

void testBoundsShareAndAdd() {
    const RockColumn c = columnAt(1.0f, 2.0f);
    const terrain::RockBounds b = terrain::rockBounds(c);
    TEST_CHECK(near(b.low, kFoot + 2.0, 1e-4) && near(b.high, kTop + 2.0, 1e-4));
    TEST_CHECK(terrain::rockShare(c, 12.0) == 1.0f);
    TEST_CHECK(terrain::rockShare(c, 40.0) == 0.0f);
    // Past kMaxRockFaces, the faces of the greatest weights are kept.
    RockColumn full;
    for (const float w : {0.9f, 0.2f, 0.7f, 0.6f, 0.1f, 1.0f}) {
        RockFace f = faceAt(0.0f);
        f.weight = w;
        full.add(f);
    }
    TEST_CHECK(full.faceCount == terrain::kMaxRockFaces);
    bool heaviest = true;
    for (int i = 0; i < full.faceCount; ++i) {
        heaviest = heaviest && full.faces[static_cast<std::size_t>(i)].weight > 0.5f;
    }
    TEST_CHECK(heaviest);
}

// Anchoring sets the heights, scales the depths of the overhangs with the height and their reach
// down only, and fades out the faces lower than twice kLeastRockFace.
void testAnchor() {
    RockFace f = faceAt(0.0f);
    f.cap = 2.0f;
    f.capDepth = 4.0f;
    f.undercut = 1.0f;
    f.undercutHeight = 2.0f;
    RockFace taller = f;
    terrain::anchorFace(taller, 100.0f, 140.0f);
    TEST_CHECK(taller.foot == 100.0f && taller.top == 140.0f && taller.weight == 1.0f);
    TEST_CHECK(taller.capDepth == 8.0f && taller.undercutHeight == 4.0f && taller.cap == 2.0f &&
               taller.undercut == 1.0f);
    RockFace lower = f;
    terrain::anchorFace(lower, 0.0f, 10.0f);
    TEST_CHECK(lower.cap == 1.0f && lower.capDepth == 2.0f && lower.weight == 1.0f);
    RockFace low = f;
    terrain::anchorFace(low, 0.0f, 4.0f);
    TEST_CHECK(low.weight > 0.0f && low.weight < 1.0f);
    RockFace reversed = f;
    terrain::anchorFace(reversed, 5.0f, 2.0f);
    TEST_CHECK(reversed.weight == 0.0f);
}

} // namespace

int main() {
    testWithoutFace();
    testStep();
    testCapAndUndercut();
    testFades();
    testGrain();
    testCarvedFace();
    testLevee();
    testBoundsShareAndAdd();
    testAnchor();
    return test::result();
}

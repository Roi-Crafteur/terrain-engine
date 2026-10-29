// The faces of the rock in three dimensions (phase 12A): without a face the density is y - ground
// bit for bit; within the run of a face the climb of the relief becomes a step, a vertical face at
// its middle between the ledges of the foot and the top, whatever the relief within the run, and
// meeting the relief at the edges of the run; its cap juts over the air and its undercut
// recedes into the rock; its grain moves it by bands, its joints recede it; below full weight it
// leans toward the relief, a single surface; the faces of a valley shape the floor its river
// carves; the levees fill; a coarse detail fades the narrow runs, thin lips, overhangs, joints and
// grain out; the bounds and the share of rock follow the faces; anchoring scales the overhangs.
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
    TEST_CHECK(terrain::rockShare(c, 12.0, {}) == 0.0f);
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
    // The relief within the run leaves the step as it is.
    TEST_CHECK(near(topOf(columnAt(-2.0f, 3.0f)), kTop, 0.6));
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

// A run of less than 3 cells fades out (the columns would alternate in and out of it); a face of
// weight below 1 leans toward the relief: its top and its foot drawn to the relief of the column
// by its weight, a lower step.
void testFades() {
    RockColumn narrow = columnAt(-1.0f);
    narrow.faces[0].run = 2.0f;
    for (const double y : {-4.0, 3.5, 11.25}) {
        TEST_CHECK(terrain::rockDensity(narrow, y, {}) == static_cast<float>(y - narrow.ground));
    }
    RockColumn behind = columnAt(-2.0f); // the relief at 14 m
    behind.faces[0].weight = 0.5f;
    TEST_CHECK(near(topOf(behind), 0.5 * (14.0 + kTop), 0.6));
    RockColumn front = columnAt(2.0f); // the relief at 6 m
    front.faces[0].weight = 0.5f;
    TEST_CHECK(near(topOf(front), 0.5 * (6.0 + kFoot), 0.6));
    behind.faces[0].weight = 0.0f;
    TEST_CHECK(terrain::rockDensity(behind, 16.0, {}) == static_cast<float>(16.0 - behind.ground));
}

// Below full weight a face leaves out its grain and its overhangs (from 0.75 of its weight they
// come in): the density of the column rises with the height, a single surface, never a slit of
// air under a ghost of a lip.
void testSingleSurfaceBelowFullWeight() {
    bool rising = true;
    for (const float distance : {-3.0f, -1.0f, 0.5f, 2.0f, 4.0f}) {
        for (const float weight : {0.2f, 0.5f, 0.74f}) {
            RockColumn c = columnAt(distance, 1.5f);
            RockFace& f = c.faces[0];
            f.cap = 3.0f;
            f.capDepth = 6.0f;
            f.undercut = 2.0f;
            f.undercutHeight = 5.0f;
            f.grain = 1.0f;
            f.joint = 1.5f;
            f.jointWidth = 4.0f;
            f.weight = weight;
            double last = -1e9;
            for (double y = -10.0; y < 40.0; y += 0.05) {
                const double d = terrain::rockDensity(c, y, {});
                rising = rising && d >= last - 1e-5;
                last = d;
            }
        }
    }
    TEST_CHECK(rising);
}

// A vertical joint recedes the face by its depth between its foot and its top, at full weight only;
// one narrower than 4 cells fades out, gone at 2 cells (the cells would catch its groove at one
// column out of two: a slit): 2 m cells leave out a joint 4 m wide.
void testJoint() {
    RockColumn c = columnAt(-1.0f); // 1 m into the rock
    c.faces[0].joint = 1.5f;
    c.faces[0].jointWidth = 4.0f;
    for (const double y : {6.0, 10.0, 14.0}) {
        TEST_CHECK(near(terrain::rockDensity(c, y, {}), 0.5, 1e-3));
    }
    TEST_CHECK(terrain::rockDensity(c, 10.0, terrain::rockDetailForLod(1)) < 0.0f);
    TEST_CHECK(terrain::rockDensity(c, 10.0, terrain::rockDetailForLod(2)) < 0.0f);
    c.faces[0].weight = 0.6f;
    TEST_CHECK(terrain::rockDensity(c, 10.0, {}) < 0.0f);
}

// A joint comes in over 4 m over the foot of its face and goes out under its top over 3 times its
// depth (the roof over its end leans 0.5 m per meter at most), a quarter of the height of the face
// at most, its depth within what fits there: never a groove cut short. On a face 8 m high, a joint
// 1.5 m deep is drawn 2/3 m deep.
void testJointEnds() {
    const auto recess = [](const RockColumn& c, double y) {
        RockColumn plain = c;
        plain.faces[0].joint = 0.0f;
        return terrain::rockDensity(c, y, {}) - terrain::rockDensity(plain, y, {});
    };
    RockColumn c = columnAt(-1.0f);
    c.faces[0].joint = 1.5f;
    c.faces[0].jointWidth = 4.0f;
    const double low = recess(c, 1.0);
    const double lower = recess(c, 3.0);
    const double middle = recess(c, 10.0);
    const double upper = recess(c, 16.0);
    const double high = recess(c, 19.5);
    TEST_CHECK(near(middle, 1.5, 1e-3));
    TEST_CHECK(low < lower && lower < middle && high < upper && upper < middle && high < 0.3);
    c.faces[0].top = 8.0f;
    TEST_CHECK(near(recess(c, 4.0), 1.0 / 1.5, 1e-3));
}

// A cap thinner than 3 cells at its tip (half its depth) fades out: 2 m deep, 1 m cells leave it
// out; 6 m deep, they draw it.
void testThinLip() {
    RockFace f = faceAt(1.0f); // 1 m into the air
    f.cap = 3.0f;
    f.capDepth = 2.0f;
    RockColumn thin;
    thin.ground = rampAt(1.0f);
    thin.uncarved = thin.ground;
    thin.add(f);
    TEST_CHECK(terrain::rockDensity(thin, 18.0, {}) > 0.0f);
    f.capDepth = 6.0f;
    RockColumn thick;
    thick.ground = thin.ground;
    thick.uncarved = thin.ground;
    thick.add(f);
    TEST_CHECK(terrain::rockDensity(thick, 18.0, {}) < 0.0f);
}

// The grain moves the face by bands up to twice its amplitude; cells of 8 m leave it out.
void testGrain() {
    // A face of 100 m over a run of 60 m, the column 0.5 m behind it on the line of the run.
    RockFace f;
    f.foot = 0.0f;
    f.top = 100.0f;
    f.distance = -0.5f;
    f.run = 60.0f;
    f.grain = 1.0f;
    f.grainShift = 3.0f;
    RockColumn c;
    c.ground = 50.0f + 100.0f * 0.5f / 60.0f;
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
    TEST_CHECK(b.low == kFoot && b.high == kTop);
    TEST_CHECK(terrain::rockShare(c, 12.0, {}) == 1.0f);
    TEST_CHECK(terrain::rockShare(c, 40.0, {}) == 0.0f);
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
    testSingleSurfaceBelowFullWeight();
    testJoint();
    testJointEnds();
    testThinLip();
    testGrain();
    testCarvedFace();
    testLevee();
    testBoundsShareAndAdd();
    testAnchor();
    return test::result();
}

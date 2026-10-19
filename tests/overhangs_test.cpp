// Overhangs of the strata on synthetic columns: the hard bank of a stratum juts out, the rock
// under it recedes down to the tread below, the density stays continuous, and the control
// mask, the zone and the support decide deterministically where this happens. Overhangs in a
// real region: sculpted_region_test.cpp. Every parameter is read from the fields.
#include "terrain/generation/overhangs.hpp"

#include "terrain/chunk_coord.hpp"
#include "terrain/generation/cliffs.hpp"
#include "terrain/generation/noise.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

using terrain::CliffColumn;
using terrain::CliffField;
using terrain::CliffParams;
using terrain::OverhangField;
using terrain::OverhangParams;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;
constexpr float kSlope = 0.5f;

// A full cliff column without river, gorge or cirque, on relief of slope kSlope.
CliffColumn makeColumn(float ground) {
    CliffColumn column;
    column.ground = ground;
    column.slope = kSlope;
    column.cliff = 1.0f;
    column.riverDistance = terrain::kNoRiverDistance;
    return column;
}

// Column whose footprint of stratum k is footprint (with the slope kSlope).
CliffColumn columnAtFootprint(const CliffField& field, int k, float footprint) {
    return makeColumn(field.stratumTop(k) - footprint * kSlope);
}

// The column with full overhangs.
CliffColumn withOverhangs(CliffColumn column, const OverhangField& overhangs) {
    overhangs.apply(column, 1.0f);
    return column;
}

// ---------------------------------------------------------------------------

// Same seed: same patches; another seed: other patches. Always in [0, 1].
void testPatchesDeterministic() {
    const OverhangField a(kSeed);
    const OverhangField b(kSeed);
    const OverhangField c(kSeed + 1);
    bool same = true;
    bool differs = false;
    bool inRange = true;
    for (int i = 0; i < 400; ++i) {
        const double x = -5000.0 + 37.3 * i;
        const double z = 2000.0 - 21.7 * i;
        const float value = a.patchAt(x, z);
        same = same && value == b.patchAt(x, z);
        differs = differs || value != c.patchAt(x, z);
        inRange = inRange && value >= 0.0f && value <= 1.0f;
    }
    TEST_CHECK(same);
    TEST_CHECK(differs);
    TEST_CHECK(inRange);
}

// The control mask is 0 where the cliff mask is below the threshold, on gentle relief and
// near the rivers; elsewhere it is the patch factor.
void testControlMask() {
    const OverhangField overhangs(kSeed);
    const OverhangParams& p = overhangs.params();
    const CliffParams cliffs;
    for (int i = 0; i < 200; ++i) {
        const double x = 311.0 * i;
        const double z = -173.0 * i;
        CliffColumn column = makeColumn(800.0f);
        TEST_CHECK(overhangs.controlAt(x, z, column, cliffs) == overhangs.patchAt(x, z));
        column.cliff = p.minCliff;
        TEST_CHECK(overhangs.controlAt(x, z, column, cliffs) == 0.0f);
        column = makeColumn(800.0f);
        column.slope = p.minSlope;
        TEST_CHECK(overhangs.controlAt(x, z, column, cliffs) == 0.0f);
        column = makeColumn(800.0f);
        column.riverDistance = p.riverFadeStart * cliffs.riverReach;
        TEST_CHECK(overhangs.controlAt(x, z, column, cliffs) == 0.0f);
        column.riverDistance = 0.0f; // on a river
        TEST_CHECK(overhangs.controlAt(x, z, column, cliffs) == 0.0f);
    }
}

// The weight sets the overhangs; a weight of 0 leaves the column as it is.
void testApply() {
    const OverhangField overhangs(kSeed);
    CliffColumn column = makeColumn(500.0f);
    overhangs.apply(column, 0.0f);
    TEST_CHECK(column.capOverhang == 0.0f && column.undercut == 0.0f);
    overhangs.apply(column, 0.5f);
    TEST_CHECK(column.capOverhang == 0.5f * overhangs.params().capOverhang);
    TEST_CHECK(column.undercut == 0.5f * overhangs.params().undercut);
    const CliffField field(kSeed);
    TEST_CHECK(overhangs.params().capOverhang <= field.maxCapOverhang());
    TEST_CHECK(overhangs.params().undercut <= field.maxUndercut());
}

// Just outside the wall of stratum k, the hard bank juts out: rock in the bank, air below it
// down to the tread of stratum k - 1, rock below that tread.
void testBankJutsOut() {
    const CliffField field(kSeed);
    const OverhangField overhangs(kSeed);
    const CliffParams& p = field.params();
    const int k = field.stratumBelow(900.0f);
    const float top = field.stratumTop(k);
    const CliffColumn plain =
        columnAtFootprint(field, k, 0.5f * overhangs.params().capOverhang);
    const CliffColumn column = withOverhangs(plain, overhangs);
    const double bank = top - 0.5 * p.capShare * p.stratumHeight;
    const double below = top - 0.7 * p.stratumHeight;
    TEST_CHECK(field.density(plain, bank) > 0.0f); // no bank without overhangs
    TEST_CHECK(field.density(column, bank) < 0.0f);
    TEST_CHECK(field.density(column, top + 0.5) > 0.0f);
    TEST_CHECK(field.density(column, below) > 0.0f);
    const double tread = field.stratumTop(k - 1);
    TEST_CHECK(field.density(column, tread + 0.5) > 0.0f);
    TEST_CHECK(field.density(column, tread - 0.5) < 0.0f);
}

// Just inside the wall, the soft rock under the bank recedes: a notch of air between the bank
// and the tread below, where the plain column is rock.
void testNotchUnderBank() {
    const CliffField field(kSeed);
    const OverhangField overhangs(kSeed);
    const CliffParams& p = field.params();
    const int k = field.stratumBelow(1200.0f);
    const float top = field.stratumTop(k);
    const CliffColumn plain = columnAtFootprint(field, k, -0.5f * overhangs.params().undercut);
    const CliffColumn column = withOverhangs(plain, overhangs);
    const double notch = top - 0.7 * p.stratumHeight;
    TEST_CHECK(field.density(plain, notch) < 0.0f);
    TEST_CHECK(field.density(column, notch) > 0.0f);
    TEST_CHECK(field.density(column, top - 0.5 * p.capShare * p.stratumHeight) < 0.0f); // bank
    TEST_CHECK(field.density(column, top + 0.5) > 0.0f);
    const double tread = field.stratumTop(k - 1); // floor of the notch
    TEST_CHECK(field.density(column, tread + 0.5) > 0.0f);
    TEST_CHECK(field.density(column, tread - 0.5) < 0.0f);
}

// Deep inside a stratum or far outside it, overhangs change no sign.
void testOverhangsOnlyAtWalls() {
    const CliffField field(kSeed);
    const OverhangField overhangs(kSeed);
    const int k = field.stratumBelow(700.0f);
    for (const float footprint : {-40.0f, -25.0f, 15.0f}) {
        const CliffColumn plain = columnAtFootprint(field, k, footprint);
        const CliffColumn column = withOverhangs(plain, overhangs);
        bool same = true;
        for (double y = plain.ground - 150.0; y <= plain.ground + 30.0; y += 0.25) {
            same = same && (field.density(plain, y) < 0.0f) == (field.density(column, y) < 0.0f);
        }
        TEST_CHECK(same);
    }
}

// Fine sweep in y: no jump, whatever the overhangs. The bound covers the slope of the ramp
// of the profile, (capOverhang + undercut) * 1.5 / (capRampShare * stratumHeight).
void testContinuousInY() {
    const CliffField field(kSeed);
    const OverhangField overhangs(kSeed);
    const CliffParams& p = field.params();
    const OverhangParams& o = overhangs.params();
    const double bound =
        1.0 + (o.capOverhang + o.undercut) * 1.5 / (p.capRampShare * p.stratumHeight);
    constexpr double kStep = 0.01;
    bool continuous = true;
    for (const float footprint : {-7.0f, -3.0f, 0.0f, 1.0f, 3.5f}) {
        const int k = field.stratumBelow(1000.0f);
        const CliffColumn column = withOverhangs(columnAtFootprint(field, k, footprint), overhangs);
        float previous = field.density(column, column.ground - 120.0);
        for (double y = column.ground - 120.0 + kStep; y <= column.ground + 20.0; y += kStep) {
            const float value = field.density(column, y);
            continuous = continuous && std::isfinite(value) &&
                         std::abs(value - previous) <= bound * kStep + 1.0e-4;
            previous = value;
        }
    }
    TEST_CHECK(continuous);
}

// The zone covers the walls of one stratum at a time, with full weight where the overhangs
// change signs.
void testZone() {
    const OverhangField overhangs(kSeed);
    const OverhangParams& o = overhangs.params();
    TEST_CHECK(overhangs.zoneWeight(0.0f) == 1.0f);
    TEST_CHECK(overhangs.zoneWeight(-o.undercut) == 1.0f);
    TEST_CHECK(overhangs.zoneWeight(o.capOverhang) == 1.0f);
    TEST_CHECK(overhangs.zoneWeight(-o.undercut - 2.0f * o.zoneMargin) == 0.0f);
    TEST_CHECK(overhangs.zoneWeight(o.capOverhang + 2.0f * o.zoneMargin) == 0.0f);
    // Strata at least 20 m apart in footprint never share the zone.
    const float width = o.undercut + o.capOverhang + 4.0f * o.zoneMargin;
    const CliffParams p;
    TEST_CHECK(width <= p.stratumHeight / p.maxSlope);
}

// Ramp of relief rising toward -x, the wall of stratum k at x = 0 (rim recession 0).
float rampRelief(const CliffField& field, int k, double x) {
    return field.stratumTop(k) - kSlope * static_cast<float>(x);
}

// A plateau (the relief keeps rising inside) supports its overhangs; a thin knob (the relief
// tops out 1 m above the stratum) does not: its walls stay, so its bank cannot float.
void testSupport() {
    const CliffField field(kSeed);
    const OverhangField overhangs(kSeed);
    const int k = field.stratumBelow(1100.0f);
    const float top = field.stratumTop(k);
    const terrain::ReliefHeight plateau = [&](double x, double) {
        return rampRelief(field, k, x);
    };
    const CliffColumn edge = makeColumn(plateau(-1.0, 0.0));
    TEST_CHECK(overhangs.weightAt(field, edge, -1.0, 0.0, 1.0f, plateau) == 1.0f);

    // Cone of slope kSlope whose summit is 1 m above the top of stratum k.
    const terrain::ReliefHeight knob = [&](double x, double z) {
        return top + 1.0f - kSlope * static_cast<float>(std::hypot(x, z));
    };
    for (const double r : {0.5, 1.0, 1.5, 2.5, 4.0}) {
        const CliffColumn column = makeColumn(knob(r, 0.0));
        TEST_CHECK(overhangs.weightAt(field, column, r, 0.0, 1.0f, knob) == 0.0f);
    }
    // Deep inside the plateau, far from any wall: no overhang either.
    const CliffColumn inside = makeColumn(plateau(-60.0, 0.0));
    TEST_CHECK(overhangs.weightAt(field, inside, -60.0, 0.0, 1.0f, plateau) == 0.0f);
    // No control, no overhang.
    TEST_CHECK(overhangs.weightAt(field, edge, -1.0, 0.0, 0.0f, plateau) == 0.0f);
}

// The weight follows the control and varies continuously across the wall.
void testWeightContinuous() {
    const CliffField field(kSeed);
    const OverhangField overhangs(kSeed);
    const int k = field.stratumBelow(1300.0f);
    const terrain::ReliefHeight plateau = [&](double x, double) {
        return rampRelief(field, k, x);
    };
    constexpr double kStep = 0.01;
    float previous = 0.0f;
    bool continuous = true;
    bool reachesOne = false;
    for (double x = -40.0; x <= 30.0; x += kStep) {
        const float weight =
            overhangs.weightAt(field, makeColumn(plateau(x, 0.0)), x, 0.0, 0.8f, plateau);
        continuous = continuous && (x == -40.0 || std::abs(weight - previous) <= 0.01f) &&
                     weight >= 0.0f && weight <= 0.8f;
        reachesOne = reachesOne || weight == 0.8f;
        previous = weight;
    }
    TEST_CHECK(continuous);
    TEST_CHECK(reachesOne);
}

} // namespace

int main() {
    testPatchesDeterministic();
    testControlMask();
    testApply();
    testBankJutsOut();
    testNotchUnderBank();
    testOverhangsOnlyAtWalls();
    testContinuousInY();
    testZone();
    testSupport();
    testWeightContinuous();
    return test::result();
}

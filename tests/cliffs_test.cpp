#include "terrain/generation/cliffs.hpp"

#include "terrain/chunk_coord.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/noise.hpp"

#include "test_assert.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using terrain::CirqueCut;
using terrain::CliffColumn;
using terrain::CliffField;
using terrain::CliffParams;
using terrain::HeightmapRegion;
using terrain::kChunkSizeMeters;
using terrain::kDefaultWorldSeed;
using terrain::kNoRiverDistance;

constexpr float kHeightTolerance = 1.0e-3f; // meters

// Deterministic pseudo-random number in [low, high), advancing the state.
double nextRandom(std::uint64_t& state, double low, double high) {
    state = terrain::noise::deriveSeed(state, 0);
    return low + (high - low) * (static_cast<double>(state >> 11) / 9007199254740992.0); // 2^53
}

bool near(float a, float b) {
    return std::fabs(a - b) <= kHeightTolerance;
}

// A column without river, gorge or cirque.
CliffColumn makeColumn(float ground, float slope = 0.5f, float cliff = 1.0f, float rim = 0.0f) {
    CliffColumn column;
    column.ground = ground;
    column.slope = slope;
    column.cliff = cliff;
    column.rimRecession = rim;
    column.riverDistance = kNoRiverDistance;
    return column;
}

// A column whose river runs 10 m away, five meters of water below the ground, with a gorge
// and two cirques around.
CliffColumn makeBusyColumn(float ground, float slope, float cliff, float rim) {
    CliffColumn column = makeColumn(ground, slope, cliff, rim);
    column.riverDistance = 10.0f;
    column.riverLevel = ground - 5.0f;
    column.gorgeHalfWidth = 40.0f;
    column.cirques[0] = CirqueCut{.distance = 25.0f, .floor = ground - 30.0f};
    column.cirques[1] = CirqueCut{.distance = 55.0f, .floor = ground - 12.0f};
    column.cirqueCount = 2;
    return column;
}

// cliff = 0: exactly the relief, whatever the other fields hold.
bool isExactRelief(const CliffField& field, const CliffColumn& column) {
    bool exact = field.topSurface(column) == column.ground;
    for (const double offset : {-300.0, -55.5, -0.3, 0.0, 0.1, 40.0, 700.0}) {
        const double y = static_cast<double>(column.ground) + offset;
        exact = exact && field.density(column, y) ==
                             static_cast<float>(y - static_cast<double>(column.ground));
    }
    return exact;
}

void testExactWithoutCliffs() {
    const CliffField field(kDefaultWorldSeed);
    for (const float ground : {-120.5f, 0.0f, 412.75f, 2999.9f}) {
        TEST_CHECK(isExactRelief(field, makeColumn(ground, 0.8f, 0.0f, 9.0f)));
        TEST_CHECK(isExactRelief(field, makeBusyColumn(ground, 0.8f, 0.0f, 9.0f)));
    }
}

struct Sweep {
    bool monotone = true;   // the density never decreases with y
    bool continuous = true; // and has no jump: every term has a slope of at most 1 in y
};

// Fine sweep of the density over +-150 m around the ground.
Sweep sweepInY(const CliffField& field, const CliffColumn& column) {
    constexpr double kStep = 0.05;
    constexpr int kSteps = 6000;
    const double start = static_cast<double>(column.ground) - 150.0;
    Sweep sweep;
    float previous = field.density(column, start);
    for (int i = 1; i <= kSteps; ++i) {
        const float d = field.density(column, start + kStep * i);
        sweep.monotone = sweep.monotone && d >= previous;
        sweep.continuous = sweep.continuous && std::fabs(d - previous) <= 1.01 * kStep + 1.0e-4;
        previous = d;
    }
    return sweep;
}

// Full and partial masks, plain columns and columns with river, gorge and cirques.
void testDensityMonotoneAndContinuousInY() {
    const CliffField field(kDefaultWorldSeed);
    const CliffParams& p = field.params();
    for (const float mask : {1.0f, 0.75f, 0.4f, 0.05f}) {
        const std::array<CliffColumn, 5> columns = {
            makeColumn(500.0f, 0.3f, mask, 0.0f),
            makeColumn(-130.25f, p.minSlope, mask, p.rimRecession),
            makeColumn(2750.5f, p.maxSlope, mask, 0.5f * p.rimRecession),
            makeBusyColumn(800.0f, 0.5f, mask, 5.0f),
            makeBusyColumn(1203.5f, 1.2f, mask, p.rimRecession),
        };
        for (const CliffColumn& column : columns) {
            const Sweep sweep = sweepInY(field, column);
            TEST_CHECK(sweep.monotone);
            TEST_CHECK(sweep.continuous);
        }
    }
}

// A linear ramp ground(x) = 500 + 0.3 x with no river, full mask and no rim recession.
constexpr double kRampBase = 500.0;
constexpr double kRampSlope = 0.3;
constexpr double kRampStep = 0.5;
constexpr int kRampSamples = 1201; // x in [-300, 300]

double rampX(int i) {
    return -300.0 + kRampStep * i;
}

CliffColumn rampColumn(double x) {
    return makeColumn(static_cast<float>(kRampBase + kRampSlope * x),
                      static_cast<float>(kRampSlope));
}

std::vector<float> rampTops(const CliffField& field) {
    std::vector<float> tops;
    tops.reserve(static_cast<std::size_t>(kRampSamples));
    for (int i = 0; i < kRampSamples; ++i) {
        tops.push_back(field.topSurface(rampColumn(rampX(i))));
    }
    return tops;
}

// Number of stratum tops between the lowest and the highest ground of the ramp.
int strataCrossedByRamp(const CliffField& field) {
    const float low = rampColumn(rampX(0)).ground;
    const float high = rampColumn(rampX(kRampSamples - 1)).ground;
    int crossed = 0;
    for (int k = field.stratumBelow(low) - 1; k <= field.stratumBelow(high) + 1; ++k) {
        const float top = field.stratumTop(k);
        if (top > low && top <= high) {
            ++crossed;
        }
    }
    return crossed;
}

// The surface of the ramp is always the flat top of the stratum just below the ground.
void testPlateausHaveFlatTops() {
    const CliffField field(kDefaultWorldSeed);
    bool flat = true;
    for (int i = 0; i < kRampSamples; ++i) {
        const CliffColumn column = rampColumn(rampX(i));
        const float tread = field.stratumTop(field.stratumBelow(column.ground));
        flat = flat && near(field.topSurface(column), tread);
    }
    TEST_CHECK(flat);
}

// Between consecutive samples the surface stays level or rises by exactly one stratum.
void testWallsAreOneStratumHigh() {
    const CliffField field(kDefaultWorldSeed);
    const std::vector<float> tops = rampTops(field);
    int walls = 0;
    bool onlyTreadsAndWalls = true;
    for (std::size_t i = 1; i < tops.size(); ++i) {
        const float step = tops[i] - tops[i - 1];
        if (std::fabs(step) < kHeightTolerance) {
            continue; // tread
        }
        if (near(step, field.params().stratumHeight)) {
            ++walls;
        } else {
            onlyTreadsAndWalls = false; // an intermediate slope
        }
    }
    TEST_CHECK(onlyTreadsAndWalls);
    TEST_CHECK(walls >= 4);
    TEST_CHECK(walls == strataCrossedByRamp(field));
}

// At a wall, every height between the two treads is rock on the high side and air on the
// low side, 0.5 m apart: the wall is vertical in the volume.
void testWallsAreVerticalInTheVolume() {
    const CliffField field(kDefaultWorldSeed);
    const std::vector<float> tops = rampTops(field);
    int checkedWalls = 0;
    bool vertical = true;
    for (std::size_t i = 1; i < tops.size(); ++i) {
        if (tops[i] - tops[i - 1] < field.params().stratumHeight - kHeightTolerance) {
            continue;
        }
        ++checkedWalls;
        const int index = static_cast<int>(i);
        const CliffColumn lowSide = rampColumn(rampX(index - 1));
        const CliffColumn highSide = rampColumn(rampX(index));
        for (const double fraction : {0.0005, 0.05, 0.25, 0.5, 0.75, 0.95, 0.9995}) {
            const double y = static_cast<double>(tops[i - 1]) +
                             fraction * static_cast<double>(tops[i] - tops[i - 1]);
            vertical = vertical && field.density(highSide, y) < 0.0f &&
                       field.density(lowSide, y) > 0.0f;
        }
    }
    TEST_CHECK(checkedWalls >= 4);
    TEST_CHECK(vertical);
}

// At a fixed height between two treads, crossing walls and treads along x never jumps:
// the footprints vary continuously, with a slope of at most 1.
void testDensityContinuousAcrossWalls() {
    constexpr double kDx = 0.01;
    constexpr int kSamples = 60001; // x in [-300, 300]
    const CliffField field(kDefaultWorldSeed);
    const double base = field.stratumTop(field.stratumBelow(static_cast<float>(kRampBase)));
    for (const double fraction : {0.2, 0.7}) {
        const double y = base + fraction * static_cast<double>(field.params().stratumHeight);
        float previous = field.density(rampColumn(-300.0), y);
        float lowest = previous;
        float highest = previous;
        bool continuous = true;
        for (int i = 1; i < kSamples; ++i) {
            const float d = field.density(rampColumn(-300.0 + kDx * i), y);
            continuous = continuous && std::fabs(d - previous) <= 1.01 * kDx + 1.0e-3;
            lowest = std::fmin(lowest, d);
            highest = std::fmax(highest, d);
            previous = d;
        }
        TEST_CHECK(continuous);
        TEST_CHECK(lowest < 0.0f && highest > 0.0f); // the sweep does cross the rock
    }
}

// The cliffs only cut: the surface is never above the relief, and at most one terrace
// drop below it.
void testCutOnlyAndBoundedDrop() {
    const CliffField field(kDefaultWorldSeed);
    const CliffParams& p = field.params();
    const float drop = field.maxTerraceDrop();
    std::uint64_t state = 3;
    bool bounded = true;
    for (int i = 0; i < 1000; ++i) {
        CliffColumn column;
        column.ground = static_cast<float>(nextRandom(state, -300.0, 3500.0));
        column.slope = static_cast<float>(nextRandom(state, p.minSlope, p.maxSlope));
        column.rimRecession = static_cast<float>(nextRandom(state, 0.0, p.rimRecession));
        column.cliff = i % 4 == 0 ? 1.0f : static_cast<float>(nextRandom(state, 0.001, 1.0));
        const float top = field.topSurface(column);
        bounded = bounded && top <= column.ground + kHeightTolerance &&
                  top >= column.ground - drop - kHeightTolerance;
    }
    TEST_CHECK(bounded);
}

// Within riverReach of a river the rock never ends below the water; farther it does.
void testRiverFloor() {
    const CliffField field(kDefaultWorldSeed);
    const CliffParams& p = field.params();
    const float top = field.stratumTop(12);
    // The stratum top is 0.875 stratum (35 m) below the ground, the water 5 m below it.
    CliffColumn column = makeColumn(top + 0.875f * p.stratumHeight);
    column.riverDistance = 10.0f;
    column.riverLevel = column.ground - 0.125f * p.stratumHeight;
    TEST_CHECK(near(field.topSurface(column), column.riverLevel));
    column.riverDistance = p.riverReach + 10.0f;
    TEST_CHECK(near(field.topSurface(column), top));
}

// A gorge cuts down to the water with vertical walls; outside it the terrace stays.
void testGorge() {
    const CliffField field(kDefaultWorldSeed);
    const CliffParams& p = field.params();
    const float top = field.stratumTop(12);
    // Terrace 5 m below the ground, water 30 m below it: a wall of 25 m at the gorge edge.
    CliffColumn column = makeColumn(top + 0.125f * p.stratumHeight);
    column.riverDistance = 10.0f;
    column.gorgeHalfWidth = 50.0f;
    column.riverLevel = column.ground - 0.75f * p.stratumHeight;
    TEST_CHECK(column.riverLevel < top);
    TEST_CHECK(near(field.topSurface(column), column.riverLevel));
    column.riverDistance = 60.0f; // outside the gorge, still within riverReach
    TEST_CHECK(column.riverDistance < p.riverReach);
    TEST_CHECK(near(field.topSurface(column), top));
}

// A cirque cuts down to its floor within cirqueRadius; farther it changes nothing.
void testCirque() {
    const CliffField field(kDefaultWorldSeed);
    const CliffParams& p = field.params();
    const float top = field.stratumTop(12);
    CliffColumn column = makeColumn(top + 0.125f * p.stratumHeight);
    const float floor = column.ground - 1.25f * p.stratumHeight; // 50 m below the ground
    column.cirques[0] = CirqueCut{.distance = 10.0f, .floor = floor};
    column.cirqueCount = 1;
    TEST_CHECK(near(field.topSurface(column), floor));
    column.cirques[0].distance = p.cirqueRadius + 10.0f;
    TEST_CHECK(near(field.topSurface(column), top));
    column.cirqueCount = 0; // an unused slot is ignored
    column.cirques[0].distance = 10.0f;
    TEST_CHECK(near(field.topSurface(column), top));
}

// Points of a regular grid, spacing meters apart, count x count points around the origin.
std::vector<std::array<double, 2>> gridPoints(int count, double spacing) {
    std::vector<std::array<double, 2>> points;
    for (int j = 0; j < count; ++j) {
        for (int i = 0; i < count; ++i) {
            points.push_back({(i - count / 2) * spacing + 0.37, (j - count / 2) * spacing - 0.81});
        }
    }
    return points;
}

// Mask and province stay within [0, 1] for any point, altitude and border weight.
void testMaskRange() {
    const CliffField field(kDefaultWorldSeed);
    bool inRange = true;
    for (const auto& point : gridPoints(13, 7000.3)) {
        const float province = field.provinceAt(point[0], point[1]);
        inRange = inRange && province >= 0.0f && province <= 1.0f;
        for (const float ground : {-100.0f, 0.0f, 80.0f, 120.0f, 500.0f, 1500.0f, 2700.0f,
                                   3000.0f, 3100.0f, 4000.0f}) {
            for (const float border : {0.0f, 0.3f, 1.0f}) {
                const float mask = field.maskAt(point[0], point[1], ground, border);
                inRange = inRange && mask >= 0.0f && mask <= 1.0f;
            }
        }
    }
    TEST_CHECK(inRange);
}

// No cliffs on oceans, coasts and summits, nor outside the region border weight.
void testMaskFades() {
    const CliffField field(kDefaultWorldSeed);
    const CliffParams& p = field.params();
    const float middle = 0.5f * (p.coastFadeHigh + p.summitFadeLow); // province only
    bool zeroed = true;
    bool followsProvince = true;
    int cliffPoints = 0;
    for (const auto& point : gridPoints(21, 5000.1)) {
        const double x = point[0];
        const double z = point[1];
        for (const float below : {-100.0f, 0.0f, 50.0f, p.coastFadeLow - 1.0f, p.coastFadeLow}) {
            zeroed = zeroed && field.maskAt(x, z, below, 1.0f) == 0.0f;
        }
        for (const float above : {p.summitFadeHigh, p.summitFadeHigh + 1.0f, 5000.0f}) {
            zeroed = zeroed && field.maskAt(x, z, above, 1.0f) == 0.0f;
        }
        zeroed = zeroed && field.maskAt(x, z, middle, 0.0f) == 0.0f;
        const float province = field.provinceAt(x, z);
        followsProvince = followsProvince &&
                          std::fabs(field.maskAt(x, z, middle, 1.0f) - province) < 1.0e-6f &&
                          std::fabs(field.maskAt(x, z, middle, 0.5f) - 0.5f * province) < 1.0e-6f;
        cliffPoints += field.maskAt(x, z, middle, 1.0f) > 0.0f ? 1 : 0;
    }
    TEST_CHECK(zeroed);
    TEST_CHECK(followsProvince);
    TEST_CHECK(cliffPoints > 0); // the mask is not zero everywhere
}

// The province takes both extremes over about 100 km, and the rim recession stays in range.
void testProvinceAndRimRanges() {
    const CliffField field(kDefaultWorldSeed);
    const float maxRim = field.params().rimRecession;
    bool sawZero = false;
    bool sawOne = false;
    bool rimInRange = true;
    float lowRim = maxRim;
    float highRim = 0.0f;
    for (const auto& point : gridPoints(41, 2500.0)) {
        const float province = field.provinceAt(point[0], point[1]);
        sawZero = sawZero || province <= 0.0f;
        sawOne = sawOne || province >= 1.0f;
        const float rim = field.rimRecessionAt(point[0], point[1]);
        rimInRange = rimInRange && rim >= 0.0f && rim <= maxRim;
        lowRim = std::fmin(lowRim, rim);
        highRim = std::fmax(highRim, rim);
    }
    TEST_CHECK(sawZero);
    TEST_CHECK(sawOne);
    TEST_CHECK(rimInRange);
    TEST_CHECK(highRim > lowRim); // the recession varies
}

// Same seed, same layers; another seed, another world.
void testDeterminismAndSeeds() {
    const CliffField a(kDefaultWorldSeed);
    const CliffField b(kDefaultWorldSeed);
    const CliffField other(kDefaultWorldSeed + 1);
    bool identical = true;
    bool provinceDiffers = false;
    for (const auto& point : gridPoints(15, 3100.7)) {
        const double x = point[0];
        const double z = point[1];
        identical = identical && a.provinceAt(x, z) == b.provinceAt(x, z) &&
                    a.maskAt(x, z, 1500.0f, 1.0f) == b.maskAt(x, z, 1500.0f, 1.0f) &&
                    a.rimRecessionAt(x, z) == b.rimRecessionAt(x, z);
        provinceDiffers = provinceDiffers || a.provinceAt(x, z) != other.provinceAt(x, z);
    }
    for (int k = -8; k <= 8; ++k) {
        identical = identical && a.stratumTop(k) == b.stratumTop(k);
    }
    TEST_CHECK(identical);
    TEST_CHECK(provinceDiffers);
}

// stratumBelow is the highest stratum whose top is at or below the height.
void testStratumBelow() {
    const CliffField field(kDefaultWorldSeed);
    bool bracketed = true;
    for (int i = 0; i < 20000; ++i) {
        const float height = -800.0f + 0.37f * static_cast<float>(i); // negative heights too
        const int k = field.stratumBelow(height);
        bracketed = bracketed && field.stratumTop(k) <= height && height < field.stratumTop(k + 1);
    }
    TEST_CHECK(bracketed);
    bool exactOnTops = true;
    for (int k = -30; k <= 30; ++k) {
        exactOnTops = exactOnTops && field.stratumBelow(field.stratumTop(k)) == k;
    }
    TEST_CHECK(exactOnTops);
}

// Tops are stratumHeight apart, the offset is in [0, stratumHeight), and the maximum drop
// follows from the parameters.
void testStrataSpacing() {
    CliffParams custom;
    custom.stratumHeight = 25.0f;
    for (const CliffParams& params : {CliffParams{}, custom}) {
        const CliffField field(kDefaultWorldSeed, params);
        bool spaced = true;
        for (int k = -60; k < 60; ++k) {
            const float gap = field.stratumTop(k + 1) - field.stratumTop(k);
            spaced = spaced && near(gap, params.stratumHeight);
        }
        TEST_CHECK(spaced);
        TEST_CHECK(field.stratumTop(0) >= 0.0f && field.stratumTop(0) < params.stratumHeight);
        const float drop = params.stratumHeight + params.rimRecession * params.maxSlope;
        TEST_CHECK(std::fabs(field.maxTerraceDrop() - drop) < 1.0e-4f);
    }
}

// The cliff site is deterministic, has the requested resolution and lies on the chunk grid.
void testFindCliffRegion() {
    const HeightmapRegion a = terrain::findCliffRegion(kDefaultWorldSeed, 25.0, 64);
    const HeightmapRegion b = terrain::findCliffRegion(kDefaultWorldSeed, 25.0, 64);
    TEST_CHECK(a.centerX == b.centerX && a.centerZ == b.centerZ);
    TEST_CHECK(a.metersPerPixel == 25.0 && a.size == 64);
    TEST_CHECK(std::fmod(a.centerX, kChunkSizeMeters) == 0.0);
    TEST_CHECK(std::fmod(a.centerZ, kChunkSizeMeters) == 0.0);

    const HeightmapRegion c = terrain::findCliffRegion(kDefaultWorldSeed, 50.0, 128);
    TEST_CHECK(c.metersPerPixel == 50.0 && c.size == 128);
    TEST_CHECK(std::fmod(c.centerX, kChunkSizeMeters) == 0.0);
    TEST_CHECK(std::fmod(c.centerZ, kChunkSizeMeters) == 0.0);
}

} // namespace

int main() {
    testExactWithoutCliffs();
    testDensityMonotoneAndContinuousInY();
    testPlateausHaveFlatTops();
    testWallsAreOneStratumHigh();
    testWallsAreVerticalInTheVolume();
    testDensityContinuousAcrossWalls();
    testCutOnlyAndBoundedDrop();
    testRiverFloor();
    testGorge();
    testCirque();
    testMaskRange();
    testMaskFades();
    testProvinceAndRimRanges();
    testDeterminismAndSeeds();
    testStratumBelow();
    testStrataSpacing();
    testFindCliffRegion();
    return test::result();
}

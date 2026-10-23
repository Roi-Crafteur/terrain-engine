// Cave districts across their real borders (Phase 8C-1), on the default world: along segments
// of 400 m crossing the border between two neighbor districts of different types (through the
// midpoint of their sites, in the direction of the line between them), the profile moves smoothly
// from the one of the first to the one of the second, the caves follow it without a jump, and
// the carved density changes between neighbors no more than it does without the border.
//
// A district only blends its profile within the blend width of its border (280 m: +-140 m around
// the bisector), so the segments end in the pure profiles of the two types.
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "cave_districts_carving_support.hpp"
#include "caves_test_support.hpp"
#include "legacy_relief_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <set>
#include <utility>
#include <vector>

namespace {

using caves_test::BoxBuilder;
using caves_test::kSeed;
using caves_test::terrainDensity;
using districts_test::Border;
using districts_test::bordersOf;
using districts_test::defaultCaves;
using districts_test::defaultProfile;
using districts_test::uniformCaves;
using districts_test::withoutAquifers;
using terrain::CaveColumn;
using terrain::CaveField;
using terrain::CaveGround;
using terrain::CaveParams;
using terrain::CaveProfile;
using terrain::CaveSampler;
using terrain::TerrainTiles;

constexpr double kHalfLength = 200.0;
constexpr double kStep = 0.5;
constexpr std::array<int, 5> kDepths{20, 35, 50, 65, 80};
// Meters per unit of a gradient noise near its zero lines, as the caves convert noise to meters
// (caves.cpp): a noise of wavelength L rises by about 1.2 over L meters.
constexpr double kNoiseSlope = 1.2;

using Fields = std::array<double, CaveProfile::kFieldCount>;

// The segment: the midpoint of the border and the unit direction from the site of the first
// district to the one of the second.
struct Segment {
    double x;
    double z;
    double ux;
    double uz;

    [[nodiscard]] double xAt(double t) const { return x + ux * t; }
    [[nodiscard]] double zAt(double t) const { return z + uz * t; }
};

Segment segmentOf(const Border& border) {
    const double dx = border.b.siteX - border.a.siteX;
    const double dz = border.b.siteZ - border.a.siteZ;
    const double length = std::hypot(dx, dz);
    return {border.x, border.z, dx / length, dz / length};
}

// Largest difference of each field between two of the profiles of the types.
Fields rangesOf(const CaveParams& params) {
    Fields ranges{};
    for (std::size_t f = 0; f < CaveProfile::kFieldCount; ++f) {
        float low = params.districts->profiles[0].values[f];
        float high = low;
        for (const CaveProfile& profile : params.districts->profiles) {
            low = std::min(low, profile.values[f]);
            high = std::max(high, profile.values[f]);
        }
        ranges[f] = static_cast<double>(high - low);
    }
    return ranges;
}

// Meters that a void moves per unit of each profile field: the radius of the spaghetti per unit
// of scale, the noise of the noodles and of the cheese per unit of offset, the half width of the
// ravines per unit of boost. The other fields (water, ice, speleothems...) do not move a void.
Fields sensitivityOf(const CaveParams& params) {
    Fields s{};
    s[CaveProfile::kSpaghettiScale] = static_cast<double>(params.spaghetti.maxRadius);
    s[CaveProfile::kNoodleOffset] = params.noodles.presenceWavelength / kNoiseSlope;
    s[CaveProfile::kCheeseOffset] = params.cheese.wavelength / kNoiseSlope;
    s[CaveProfile::kRavineBoost] = static_cast<double>(params.ravines.maxHalfWidth);
    return s;
}

// How much the caves differ between the two sides of a border: the largest move of a void
// between the two profiles.
double contrastOf(const Border& border, const Fields& sensitivity) {
    const CaveProfile a = defaultProfile(border.a.type);
    const CaveProfile b = defaultProfile(border.b.type);
    double contrast = 0.0;
    for (std::size_t f = 0; f < CaveProfile::kFieldCount; ++f) {
        contrast += sensitivity[f] * std::abs(static_cast<double>(a.values[f] - b.values[f]));
    }
    return contrast;
}

// The borders of the most different caves, no two of the same pair of types.
std::vector<Border> mostContrasted(std::vector<Border> borders, const Fields& sensitivity,
                                   std::size_t count) {
    std::stable_sort(borders.begin(), borders.end(), [&](const Border& a, const Border& b) {
        return contrastOf(a, sensitivity) > contrastOf(b, sensitivity);
    });
    std::set<std::pair<int, int>> pairs;
    std::vector<Border> picked;
    for (const Border& border : borders) {
        const int low = std::min(static_cast<int>(border.a.type), static_cast<int>(border.b.type));
        const int high = std::max(static_cast<int>(border.a.type), static_cast<int>(border.b.type));
        if (picked.size() < count && pairs.insert({low, high}).second) {
            picked.push_back(border);
        }
    }
    return picked;
}

// Box of the segment, from 100 m under the ground to 2 m over it.
terrain::CaveBox boxAlong(const CaveGround& ground, const Segment& seg) {
    BoxBuilder builder;
    for (double t = -kHalfLength; t <= kHalfLength; t += kStep) {
        const auto height = static_cast<double>(ground.height(seg.xAt(t), seg.zAt(t)));
        builder.add(seg.xAt(t), height - 100.0, seg.zAt(t));
        builder.add(seg.xAt(t), height + 2.0, seg.zAt(t));
    }
    return builder.box(1.0);
}

struct Totals {
    double profileStep = 0.0; // largest move of a field in a step, as a share of its range
    double voidStep = 0.0;    // largest move of a void in a step with the profile of the next
    double hardSwitch = 0.0;  // largest move of a void between the two pure profiles
    std::size_t monotoneBorders = 0;
    std::size_t pureEnds = 0;
    std::size_t carved = 0;
    bool monotone = true;
    bool pure = true;
    bool columnsAgree = true;
    bool jumpsBounded = true;
};

// The profile along the segment: its steps, whether every field moves from the profile of the
// first type to the one of the second without turning back (where only the two districts weigh),
// and that it is exactly the profile of a type where a single district weighs.
void checkProfile(CaveSampler& sampler, const Border& border, const Segment& seg,
                  const Fields& ranges, Totals& totals) {
    const CaveProfile a = defaultProfile(border.a.type);
    const CaveProfile b = defaultProfile(border.b.type);
    int mostDistricts = 0;
    bool monotone = true;
    CaveProfile previous = sampler.profileAt(seg.xAt(-kHalfLength), seg.zAt(-kHalfLength));
    for (double t = -kHalfLength; t <= kHalfLength; t += kStep) {
        const double x = seg.xAt(t);
        const double z = seg.zAt(t);
        const CaveProfile profile = sampler.profileAt(x, z);
        const terrain::DistrictWeights weights = sampler.districtWeights(x, z);
        mostDistricts = std::max(mostDistricts, weights.count);
        totals.columnsAgree =
            totals.columnsAgree &&
            sampler.column(x, z, sampler.ground().height(x, z)).profile == profile;
        if (weights.count == 1) {
            totals.pure = totals.pure && profile == defaultProfile(weights.districts[0].type);
            totals.pureEnds += t == -kHalfLength || t == kHalfLength ? 1 : 0;
        }
        for (std::size_t f = 0; f < CaveProfile::kFieldCount; ++f) {
            const double move = static_cast<double>(profile.values[f] - previous.values[f]);
            totals.profileStep =
                std::max(totals.profileStep, ranges[f] > 0.0 ? std::abs(move) / ranges[f] : 0.0);
            const double way = static_cast<double>(b.values[f] - a.values[f]);
            monotone = monotone && way * move >= -1.0e-6;
        }
        previous = profile;
    }
    if (mostDistricts <= 2) {
        totals.monotone = totals.monotone && monotone;
        ++totals.monotoneBorders;
    }
}

// Voids at the positions of the segment: with the profile of the next sample instead of its own
// (a step of the blend), and with each of the two pure profiles (a hard switch).
void checkVoids(CaveSampler& sampler, const Border& border, const Segment& seg, Totals& totals) {
    const CaveProfile a = defaultProfile(border.a.type);
    const CaveProfile b = defaultProfile(border.b.type);
    for (const int depth : kDepths) {
        for (double t = -kHalfLength; t < kHalfLength; t += kStep) {
            const double x = seg.xAt(t);
            const double z = seg.zAt(t);
            const float height = sampler.ground().height(x, z);
            const double y = static_cast<double>(height) - depth;
            const CaveColumn column = sampler.column(x, z, height);
            const float here = sampler.voidAt(column, y);
            CaveColumn next = column;
            next.profile = sampler.profileAt(seg.xAt(t + kStep), seg.zAt(t + kStep));
            totals.voidStep = std::max(
                totals.voidStep, static_cast<double>(std::abs(sampler.voidAt(next, y) - here)));
            CaveColumn first = column;
            CaveColumn second = column;
            first.profile = a;
            second.profile = b;
            totals.hardSwitch = std::max(totals.hardSwitch,
                                         static_cast<double>(std::abs(sampler.voidAt(first, y) -
                                                                      sampler.voidAt(second, y))));
        }
    }
}

// Largest change of the carved density between two neighbors of the segment, over the depths
// below the ground, and the samples the caves carved.
double largestJump(const CaveField& field, const CaveGround& ground, const Segment& seg,
                   std::size_t& carved) {
    CaveSampler sampler(field, ground, withoutAquifers(), boxAlong(ground, seg));
    double largest = 0.0;
    for (const int depth : kDepths) {
        float previous = 0.0f;
        for (double t = -kHalfLength; t <= kHalfLength; t += kStep) {
            const double x = seg.xAt(t);
            const double z = seg.zAt(t);
            const float height = ground.height(x, z);
            const double y = static_cast<double>(height) - depth;
            const float rock = terrainDensity(y, height);
            const float density = sampler.density(sampler.column(x, z, height), y, rock);
            largest = t > -kHalfLength
                          ? std::max(largest, static_cast<double>(std::abs(density - previous)))
                          : largest;
            carved += density != rock ? 1 : 0;
            previous = density;
        }
    }
    return largest;
}

// The carved density along the segment jumps at most twice as much as the largest jump of the
// same segment where nothing blends: with neutral profiles, and with each side's pure profile
// everywhere. The steep shell where the density fades back to the terrain around the caves (tens
// of meters of density per meter of void) makes these jumps large, and the same on both sides.
void checkJumps(const CaveGround& ground, const Border& border, const Segment& seg,
                const CaveField& field, Totals& totals) {
    const CaveField neutral(kSeed, uniformCaves(CaveProfile{}));
    const CaveField a(kSeed, uniformCaves(defaultProfile(border.a.type)));
    const CaveField b(kSeed, uniformCaves(defaultProfile(border.b.type)));
    std::size_t ignored = 0;
    const double blended = largestJump(field, ground, seg, totals.carved);
    const double baseline =
        std::max({largestJump(neutral, ground, seg, ignored), largestJump(a, ground, seg, ignored),
                  largestJump(b, ground, seg, ignored)});
    std::printf("  %s | %s: density jump %.2f blended, %.2f without the border\n",
                terrain::caveDistrictName(border.a.type), terrain::caveDistrictName(border.b.type),
                blended, baseline);
    totals.jumpsBounded = totals.jumpsBounded && blended <= 2.0 * baseline + 0.25;
}

void testBorders() {
    const TerrainTiles tiles(kSeed, test_relief::legacyRelief(), {}, defaultCaves());
    const CaveField& field = *tiles.caves();
    const CaveGround& ground = tiles.caveGround();
    const CaveParams& params = field.params();
    const Fields ranges = rangesOf(params);
    const Fields sensitivity = sensitivityOf(params);
    const std::vector<Border> borders =
        mostContrasted(bordersOf(field, ground, {-8, 16}, 0.0f, 800.0f), sensitivity, 5);
    TEST_CHECK(borders.size() == 5);
    // A step of 0.5 m moves the excess distance of a district by at most 1 m, its weight by at
    // most 1.5 / blend of that (the slope of the smoothstep): 3 * step / blend of the difference
    // of two profiles per district whose weight changes; 4 of them, to be generous.
    const double stepShare = 4.0 * 3.0 * kStep / params.districts->blend;
    double sensitivityTotal = 0.0;
    for (std::size_t f = 0; f < CaveProfile::kFieldCount; ++f) {
        sensitivityTotal += sensitivity[f] * ranges[f];
    }
    Totals totals;
    for (const Border& border : borders) {
        const Segment seg = segmentOf(border);
        CaveSampler sampler(field, ground, withoutAquifers(), boxAlong(ground, seg));
        checkProfile(sampler, border, seg, ranges, totals);
        checkVoids(sampler, border, seg, totals);
        checkJumps(ground, border, seg, field, totals);
    }
    const double voidBound = stepShare * sensitivityTotal;
    std::printf("borders: profile moves at most %.4f of its range per step (bound %.4f); a void "
                "moves at most %.3f m per step (bound %.3f) against %.1f m for a hard switch; %zu "
                "carved samples\n",
                totals.profileStep, stepShare, totals.voidStep, voidBound, totals.hardSwitch,
                totals.carved);
    TEST_CHECK(totals.columnsAgree && totals.pure && totals.pureEnds >= 2 * borders.size());
    TEST_CHECK(totals.monotone && totals.monotoneBorders >= 2);
    TEST_CHECK(totals.profileStep > 0.0 && totals.profileStep <= stepShare);
    TEST_CHECK(totals.voidStep <= voidBound);
    // The checks have teeth: switching the profile at once would move a void far more.
    TEST_CHECK(totals.hardSwitch >= 5.0 * voidBound);
    TEST_CHECK(totals.carved >= 1000 && totals.jumpsBounded);
}

} // namespace

int main() {
    testBorders();
    return test::result();
}

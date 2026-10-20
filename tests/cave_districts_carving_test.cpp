// Carving of the cave districts (Phase 8C-1): the neutral profile changes nothing, each profile
// shifts the voids of the components it declares (spaghetti, noodles, cheese, ravines) in the
// declared direction and leaves the others bit for bit, and the lower bounds that skip the noise
// cells stay exact for every profile (the carved density is the full computation at the details
// of LOD 0 to 2). Borders: cave_districts_borders_test.cpp; water: cave_districts_water_test.cpp
// and cave_districts_lakes_test.cpp; speleothems: cave_districts_speleothems_test.cpp; tiles:
// cave_districts_tiles_test.cpp and cave_districts_seams_test.cpp.
//
// The effect of one profile is measured apart from the random draw of the types: caves whose
// districts ALL behave as that profile (uniformCaves), against the same caves with every profile
// neutral, same seed, same ground, same samples.
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/streaming/cave_tiles.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "cave_districts_carving_support.hpp"
#include "cave_environment_test_support.hpp"
#include "caves_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace {

using caves_test::boxOf;
using caves_test::flatGround;
using caves_test::heightfieldGround;
using caves_test::humidEnvironment;
using caves_test::kSeed;
using caves_test::Point;
using caves_test::randomPoints;
using caves_test::Rng;
using caves_test::terrainDensity;
using caves_test::Window;
using caves_test::withEnvironment;
using districts_test::Border;
using districts_test::bordersOf;
using districts_test::defaultProfile;
using districts_test::denseCaves;
using districts_test::districtsOf;
using districts_test::forEachSample;
using districts_test::uniformCaves;
using districts_test::withoutAquifers;
using terrain::CaveBox;
using terrain::CaveColumn;
using terrain::CaveDetail;
using terrain::CaveDistrictType;
using terrain::CaveField;
using terrain::CaveGround;
using terrain::CaveProfile;
using terrain::CaveSampler;
using terrain::CaveVoids;
using terrain::Heightfield;

constexpr float kFlatHeight = 300.0f;
constexpr int kDepthStep = 2;
constexpr int kComponents = 4; // spaghetti, noodles, cheese, ravines
constexpr const char* kComponentNames[kComponents] = {"spaghetti", "noodles", "cheese", "ravines"};

// ---------------------------------------------------------------------------

// Districts whose profiles are all neutral leave the caves, their water and their columns exactly
// as without districts, on relief and over wet ground.
void testNeutralIsIdentical() {
    const CaveField plain(kSeed);
    const CaveField neutral(kSeed, uniformCaves(CaveProfile{}));
    const Heightfield heightfield(kSeed);
    const CaveGround ground = withEnvironment(heightfieldGround(heightfield), humidEnvironment());
    Rng rng(41);
    const std::vector<Point> points =
        randomPoints(ground, rng, 3000, {2000.0, 1000.0, 256.0}, -5.0, 110.0);
    CaveSampler a(plain, ground, CaveDetail{}, boxOf(points));
    CaveSampler b(neutral, ground, CaveDetail{}, boxOf(points));
    bool same = true;
    bool neutralProfile = true;
    int carved = 0;
    int wet = 0;
    for (const Point& p : points) {
        const CaveColumn ca = a.column(p.x, p.z, p.ground);
        const CaveColumn cb = b.column(p.x, p.z, p.ground);
        const float rock = terrainDensity(p.y, p.ground);
        const float density = a.density(ca, p.y, rock);
        const float water = a.waterDensity(ca, p.y, rock, density);
        same = same && density == b.density(cb, p.y, rock) &&
               a.voidAt(ca, p.y) == b.voidAt(cb, p.y) &&
               water == b.waterDensity(cb, p.y, rock, density) && ca.waterLevel == cb.waterLevel &&
               ca.frozen == cb.frozen && ca.barrierCount == cb.barrierCount;
        neutralProfile =
            neutralProfile && ca.profile == CaveProfile{} && cb.profile == CaveProfile{};
        carved += density != rock ? 1 : 0;
        wet += water < 0.0f ? 1 : 0;
    }
    std::printf("neutral: %zu points, %d carved, %d with water, all as without districts\n",
                points.size(), carved, wet);
    TEST_CHECK(same && neutralProfile);
    TEST_CHECK(carved >= 200 && wet >= 50);
}

// ---------------------------------------------------------------------------

using Components = std::array<float, kComponents>;

Components componentsOf(const CaveVoids& v) {
    return {v.spaghetti, v.noodles, v.cheese, v.ravines};
}

// How a profile changes each component with respect to the neutral one: +1 carves more (smaller
// voids), -1 less, 0 not at all.
std::array<int, kComponents> directionsOf(const CaveProfile& profile) {
    const auto sign = [](float value) { return value > 0.0f ? 1 : (value < 0.0f ? -1 : 0); };
    return {sign(profile[CaveProfile::kSpaghettiScale] - 1.0f),
            sign(-profile[CaveProfile::kNoodleOffset]), sign(-profile[CaveProfile::kCheeseOffset]),
            sign(profile[CaveProfile::kRavineBoost])};
}

// Voids of the components at the samples of the windows, on the detail without aquifers.
std::vector<CaveVoids> voidsOver(const CaveField& field, const CaveGround& ground,
                                 const std::vector<Window>& windows) {
    std::vector<CaveVoids> all;
    for (const Window& w : windows) {
        forEachSample(field, ground, withoutAquifers(), w, kDepthStep,
                      [&all](CaveSampler& sampler, const CaveColumn& column, double y, float) {
                          all.push_back(sampler.voids(column, y));
                      });
    }
    return all;
}

struct ComponentAir {
    std::array<std::size_t, kComponents> air{}; // samples where the component alone carves
    std::size_t wrongWay = 0;                   // samples whose void moved against the profile
};

// Air of each component in voids (within the limits), and how many voids moved against the
// directions with respect to the neutral ones: the relations are exact (the same noise, a
// monotone profile), not statistical.
ComponentAir compare(const std::vector<CaveVoids>& neutral, const std::vector<CaveVoids>& voids,
                     const std::array<int, kComponents>& directions) {
    ComponentAir result;
    for (std::size_t n = 0; n < voids.size(); ++n) {
        const Components before = componentsOf(neutral[n]);
        const Components after = componentsOf(voids[n]);
        for (std::size_t c = 0; c < kComponents; ++c) {
            result.air[c] += after[c] < 0.0f && voids[n].limit < 0.0f ? 1 : 0;
            const int d = directions[c];
            const bool wrong = d > 0 ? after[c] > before[c]
                                     : (d < 0 ? after[c] < before[c] : after[c] != before[c]);
            result.wrongWay += wrong ? 1 : 0;
        }
    }
    return result;
}

std::vector<Window> componentWindows(const CaveField& field) {
    const auto roof = static_cast<int>(field.params().minRoof);
    const auto deepest = static_cast<int>(field.params().maxDepth);
    return {{-2000.0, -1000.0, 48.0, 48.0, 4.0, roof, deepest},
            {2000.0, -1500.0, 48.0, 48.0, 4.0, roof, deepest},
            {-3000.0, 800.0, 48.0, 48.0, 4.0, roof, deepest}};
}

// Each type, as the profile of every district, against the neutral caves over the same samples:
// every component moves as the profile declares (strictly more or less air for the three noise
// components, bit for bit when it declares nothing), and the named effects of the types hold.
void testComponents() {
    const CaveGround ground = flatGround(kFlatHeight);
    const CaveField neutralField(kSeed, uniformCaves(CaveProfile{}));
    const std::vector<Window> windows = componentWindows(neutralField);
    const std::vector<CaveVoids> neutral = voidsOver(neutralField, ground, windows);
    const ComponentAir base = compare(neutral, neutral, {});
    std::array<ComponentAir, terrain::kCaveDistrictTypeCount> result;
    std::printf("components over %zu samples, air of the neutral caves:", neutral.size());
    for (std::size_t c = 0; c < kComponents; ++c) {
        std::printf(" %s %zu", kComponentNames[c], base.air[c]);
    }
    std::printf("\n");
    for (int t = 0; t < terrain::kCaveDistrictTypeCount; ++t) {
        const auto type = static_cast<CaveDistrictType>(t);
        const CaveProfile profile = defaultProfile(type);
        const CaveField field(kSeed, uniformCaves(profile));
        const std::array<int, kComponents> directions = directionsOf(profile);
        const ComponentAir air = compare(neutral, voidsOver(field, ground, windows), directions);
        result[static_cast<std::size_t>(t)] = air;
        std::printf("  %-9s spaghetti %5zu noodles %5zu cheese %5zu\n",
                    terrain::caveDistrictName(type), air.air[0], air.air[1], air.air[2]);
        TEST_CHECK(air.wrongWay == 0);
        for (std::size_t c = 0; c < 3; ++c) { // the ravines are measured around an axis
            const int d = directions[c];
            TEST_CHECK(d > 0 ? air.air[c] > base.air[c]
                             : (d < 0 ? air.air[c] < base.air[c] || base.air[c] == 0
                                      : air.air[c] == base.air[c]));
        }
    }
    const auto air = [&result](CaveDistrictType type) {
        return result[static_cast<std::size_t>(type)].air;
    };
    // Tunnels: wider spaghetti (1.35 times the radius: about 1.8 times the section) and more
    // noodles; halls: far more cheese and fewer tunnels; rivers: wider tunnels.
    TEST_CHECK(2 * air(CaveDistrictType::Tunnels)[0] >= 3 * base.air[0]);
    TEST_CHECK(air(CaveDistrictType::Tunnels)[1] * 5 >= base.air[1] * 6);
    TEST_CHECK(air(CaveDistrictType::Halls)[2] >= 300 &&
               air(CaveDistrictType::Halls)[2] >= 20 * base.air[2]);
    TEST_CHECK(air(CaveDistrictType::Halls)[0] < base.air[0]);
    TEST_CHECK(air(CaveDistrictType::Rivers)[0] * 5 >= base.air[0] * 6);
}

// ---------------------------------------------------------------------------

// A point on the axis of a ravine (within 1 m) where the neutral caves hold none: the ravine
// strength is 0 around it, over the 48 m square. Searched on a coarse then a fine lattice.
bool findBareAxis(const CaveField& plain, const CaveGround& ground, double& axisX, double& axisZ) {
    constexpr double kReach = 2048.0;
    constexpr double kCoarse = 32.0;
    constexpr double kSquare = 24.0;
    const auto bare = [&](double x, double z) {
        return plain.column(x, z, kFlatHeight, ground, {}).ravineStrength == 0.0f;
    };
    for (double z = -kReach; z <= kReach; z += kCoarse) {
        for (double x = -kReach; x <= kReach; x += kCoarse) {
            const CaveColumn near = plain.column(x, z, kFlatHeight, ground, {});
            if (near.ravineStrength > 0.0f || near.ravineDistance > 20.0f) {
                continue;
            }
            for (double dz = -kCoarse / 2.0; dz <= kCoarse / 2.0; dz += 1.0) {
                for (double dx = -kCoarse / 2.0; dx <= kCoarse / 2.0; dx += 1.0) {
                    const CaveColumn c = plain.column(x + dx, z + dz, kFlatHeight, ground, {});
                    if (c.ravineDistance <= 1.0f && bare(x + dx, z + dz) &&
                        bare(x + dx - kSquare, z + dz - kSquare) &&
                        bare(x + dx + kSquare, z + dz + kSquare) &&
                        bare(x + dx - kSquare, z + dz + kSquare) &&
                        bare(x + dx + kSquare, z + dz - kSquare)) {
                        axisX = x + dx;
                        axisZ = z + dz;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

// Crevasses: the ravine strength is boosted to 1 everywhere, so a ravine axis where the neutral
// caves hold none opens a ravine all along it; half the boost opens a narrower one.
void testCrevasses() {
    const CaveGround ground = flatGround(kFlatHeight);
    const CaveField plain(kSeed);
    double axisX = 0.0;
    double axisZ = 0.0;
    TEST_CHECK(findBareAxis(plain, ground, axisX, axisZ));
    CaveProfile half = defaultProfile(CaveDistrictType::Crevasses);
    half[CaveProfile::kRavineBoost] = 0.5f;
    const CaveField neutralField(kSeed, uniformCaves(CaveProfile{}));
    const CaveField halfField(kSeed, uniformCaves(half));
    const CaveField fullField(kSeed, uniformCaves(defaultProfile(CaveDistrictType::Crevasses)));
    const auto roof = static_cast<int>(plain.params().minRoof);
    const std::vector<Window> windows = {{axisX - 24.0, axisZ - 24.0, 48.0, 48.0, 2.0, roof,
                                          static_cast<int>(plain.params().maxDepth)}};
    const std::vector<CaveVoids> before = voidsOver(neutralField, ground, windows);
    const ComponentAir none = compare(before, before, {});
    const ComponentAir some =
        compare(before, voidsOver(halfField, ground, windows), directionsOf(half));
    const ComponentAir all = compare(before, voidsOver(fullField, ground, windows),
                                     directionsOf(defaultProfile(CaveDistrictType::Crevasses)));
    std::printf("ravine at %.0f, %.0f: ravine air %zu neutral, %zu with half the boost, %zu with "
                "crevasses\n",
                axisX, axisZ, none.air[3], some.air[3], all.air[3]);
    TEST_CHECK(none.air[3] == 0);
    TEST_CHECK(some.air[3] >= 100 && all.air[3] > some.air[3] + 100);
    TEST_CHECK(some.wrongWay == 0 && all.wrongWay == 0);
}

// ---------------------------------------------------------------------------

// The profile of a column feeds the lower bound of its noise cell: for every profile, at all the
// details that carve noises, the smallest void of the noise components at a point is never below
// the bound of its cell (clamped as the voids are), so skipping a cell never skips a cave. With
// assumed, the bound is computed for that profile instead of the one of the column: a bound blind
// to the profile, which the check must catch.
std::size_t violatedBounds(const CaveField& field, const CaveGround& ground,
                           const std::vector<Point>& points, int lod, std::size_t& near,
                           const CaveProfile* assumed = nullptr) {
    const CaveDetail detail = terrain::caveDetailForLod(lod);
    const CaveBox box = boxOf(points);
    CaveSampler sampler(field, ground, detail, box);
    terrain::CaveNoiseCache cache(field.lattice(), box);
    const float clamp = field.params().voidClamp;
    std::size_t violated = 0;
    for (const Point& p : points) {
        const CaveColumn column = sampler.column(p.x, p.z, p.ground);
        terrain::CaveNoiseValues low{};
        terrain::CaveNoiseValues high{};
        cache.cellBounds(p.x, p.y, p.z, field.noisesOf(detail), low, high);
        const float bound =
            field.noiseLowerBound(field.cellBound(low, high, detail),
                                  assumed != nullptr ? *assumed : column.profile, detail);
        const CaveVoids v = sampler.voids(column, p.y);
        const float smallest = std::min({v.spaghetti, v.noodles, v.cheese});
        violated += smallest < std::max(bound, -clamp) - 1.0e-3f ? 1 : 0;
        near += smallest - bound < 1.0f ? 1 : 0;
    }
    return violated;
}

CaveProfile halfway(const CaveProfile& a, const CaveProfile& b) {
    CaveProfile result;
    for (std::size_t f = 0; f < CaveProfile::kFieldCount; ++f) {
        result.values[f] = (a.values[f] + b.values[f]) / 2.0f;
    }
    return result;
}

// The bounds hold for the profile of every type and for a blend of two of them, at the details of
// LOD 0 to 2, at random points over flat ground; a bound blind to the profile would not.
void testBoundsAreExact() {
    const CaveGround ground = flatGround(kFlatHeight);
    Rng rng(43);
    const std::vector<Point> points = randomPoints(ground, rng, 600, {0.0, 0.0, 150.0}, 5.0, 110.0);
    std::vector<CaveProfile> profiles;
    for (int t = 0; t < terrain::kCaveDistrictTypeCount; ++t) {
        profiles.push_back(defaultProfile(static_cast<CaveDistrictType>(t)));
    }
    profiles.push_back(halfway(defaultProfile(CaveDistrictType::Tunnels),
                               defaultProfile(CaveDistrictType::Halls)));
    std::size_t violated = 0;
    std::size_t blind = 0;
    std::size_t near = 0;
    std::size_t ignored = 0;
    std::size_t checked = 0;
    const CaveProfile neutral;
    for (const CaveProfile& profile : profiles) {
        const CaveField field(kSeed, uniformCaves(profile));
        for (int lod = 0; lod <= 2; ++lod) {
            violated += violatedBounds(field, ground, points, lod, near);
            blind += violatedBounds(field, ground, points, lod, ignored, &neutral);
            checked += points.size();
        }
    }
    std::printf("bounds: %zu points over %zu profiles and 3 details, %zu within 1 m of their "
                "bound, %zu below it (%zu below a bound blind to the profile)\n",
                checked, profiles.size(), near, violated, blind);
    TEST_CHECK(violated == 0);
    TEST_CHECK(near > 0);
    TEST_CHECK(blind > 0); // the check has teeth: the profile matters to the bounds
}

// ---------------------------------------------------------------------------

struct SkipCounts {
    std::size_t samples = 0;
    std::size_t inexact = 0; // carved density different from the full computation
    std::size_t carved = 0;  // samples the caves changed
};

// The carved density, which skips the noise cells whose bound proves no cave, equals the
// density of the full computation at every sample of the window, at the detail of a LOD.
void countSkips(const CaveField& field, const CaveGround& ground, const Window& w, int lod,
                SkipCounts& counts) {
    const CaveDetail detail = terrain::caveDetailForLod(lod);
    forEachSample(field, ground, detail, w, 1,
                  [&](CaveSampler& sampler, const CaveColumn& column, double y, float rock) {
                      const float carved = sampler.density(column, y, rock);
                      ++counts.samples;
                      counts.inexact +=
                          carved == field.carve(rock, sampler.voidAt(column, y), detail) ? 0 : 1;
                      counts.carved += carved != rock ? 1 : 0;
                  });
}

// Windows at the sites of the first district of each wanted type, and at the midpoint of two
// borders (blended profiles), over the ground.
std::vector<Window> districtWindows(const CaveField& field, const CaveGround& ground,
                                    float minHeight, std::vector<CaveDistrictType>& types) {
    constexpr double kSize = 36.0;
    const auto roof = static_cast<int>(field.params().minRoof);
    const auto deepest = static_cast<int>(field.params().maxDepth);
    std::vector<Window> windows;
    for (int t = 0; t < terrain::kCaveDistrictTypeCount; ++t) {
        const auto type = static_cast<CaveDistrictType>(t);
        const std::vector<terrain::CaveDistrict> found =
            districtsOf(field, ground, {-6, 12}, type, minHeight);
        if (!found.empty() && windows.size() < 6) {
            windows.push_back({found.front().siteX - kSize / 2.0, found.front().siteZ - kSize / 2.0,
                               kSize, kSize, 3.0, roof, deepest});
            types.push_back(type);
        }
    }
    const std::vector<Border> borders = bordersOf(field, ground, {-6, 12}, minHeight, 1.0e9f);
    for (std::size_t n = 0; n < std::min<std::size_t>(borders.size(), 2); ++n) {
        windows.push_back({borders[n].x - kSize / 2.0, borders[n].z - kSize / 2.0, kSize, kSize,
                           3.0, roof, deepest});
    }
    return windows;
}

// Over small districts (several types, tunnels among them, and their borders) on flat ground and
// on relief, at the three details that carve noises.
void testSkipsAreExact() {
    const CaveField field(kSeed, denseCaves());
    const Heightfield heightfield(kSeed);
    const CaveGround flat = withEnvironment(flatGround(100.0f), humidEnvironment());
    const CaveGround relief = withEnvironment(heightfieldGround(heightfield), humidEnvironment());
    for (const CaveGround* ground : {&flat, &relief}) {
        std::vector<CaveDistrictType> types;
        const std::vector<Window> windows =
            districtWindows(field, *ground, ground == &flat ? 0.0f : 20.0f, types);
        TEST_CHECK(std::count(types.begin(), types.end(), CaveDistrictType::Tunnels) == 1);
        TEST_CHECK(types.size() >= 4);
        for (int lod = 0; lod <= 2; ++lod) {
            SkipCounts counts;
            for (const Window& w : windows) {
                countSkips(field, *ground, w, lod, counts);
            }
            std::printf("skips (%s) at LOD %d: %zu windows of %zu types, %zu samples, %zu carved, "
                        "%zu inexact\n",
                        ground == &flat ? "flat" : "relief", lod, windows.size(), types.size(),
                        counts.samples, counts.carved, counts.inexact);
            TEST_CHECK(counts.inexact == 0);
            TEST_CHECK(counts.carved >= 1000);
        }
    }
}

} // namespace

int main() {
    testNeutralIsIdentical();
    testComponents();
    testCrevasses();
    testBoundsAreExact();
    testSkipsAreExact();
    return test::result();
}

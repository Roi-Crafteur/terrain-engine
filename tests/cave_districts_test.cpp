// Cave districts (cave_regions.hpp), the cells themselves: deterministic districts (same seed,
// same sites and types, whatever the order they are read in), sites inside their jittered cells,
// the limits of the constructor, the type drawn by the climate and the height of the site over
// many cells, the names and default profiles of the types, and the types of the real world. The
// weights and the blending of the profiles are in cave_district_weights_test.cpp.
#include "terrain/generation/aquifers.hpp"
#include "terrain/generation/cave_regions.hpp"
#include "terrain/generation/caves.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "cave_districts_test_support.hpp"
#include "legacy_relief_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <string>

namespace {

using districts_test::constantSite;
using districts_test::countOf;
using districts_test::countTypes;
using districts_test::hashUnit;
using districts_test::kSeed;
using districts_test::kWindowCellX;
using districts_test::kWindowCellZ;
using districts_test::TypeCounts;
using districts_test::variedSite;
using districts_test::World;
using terrain::CaveDistrict;
using terrain::CaveDistrictParams;
using terrain::CaveDistricts;
using terrain::CaveDistrictType;
using terrain::CaveEnvironment;
using terrain::CaveProfile;
using terrain::DistrictWeights;

constexpr int kTypeCount = terrain::kCaveDistrictTypeCount;

bool sameWeights(const DistrictWeights& a, const DistrictWeights& b) {
    bool same = a.count == b.count;
    for (int i = 0; same && i < a.count; ++i) {
        const auto slot = static_cast<std::size_t>(i);
        same = a.districts[slot] == b.districts[slot] && a.weights[slot] == b.weights[slot];
    }
    return same;
}

// True if the environment and the ground height of a site allow a district of this type.
bool allowed(CaveDistrictType type, const CaveEnvironment& environment, float height,
             float floodedMaxHeight) {
    switch (type) {
    case CaveDistrictType::Glacier:
        return environment.temperature <= 0.0f;
    case CaveDistrictType::Volcanic:
        return environment.temperature > 0.0f;
    case CaveDistrictType::Lush:
        return environment.temperature >= 10.0f && environment.humidity >= 0.55f;
    case CaveDistrictType::Flooded:
        return height < floodedMaxHeight;
    default:
        return true;
    }
}

// ---------------------------------------------------------------------------

// The same seed gives the same districts and weights, whatever the order they are read in;
// another seed gives other sites and, over a varied climate, other types.
void testDeterminism() {
    const CaveDistrictParams params;
    World a(kSeed, params, variedSite());
    World b(kSeed, params, variedSite());
    World other(kSeed + 1, params, variedSite());
    World reversed(kSeed, params, variedSite());
    constexpr int kHalf = 12;
    int cells = 0;
    int otherSites = 0;
    int otherTypes = 0;
    bool same = true;
    bool sameReversed = true;
    for (int cellZ = kHalf - 1; cellZ >= -kHalf; --cellZ) {
        for (int cellX = kHalf - 1; cellX >= -kHalf; --cellX) {
            sameReversed = sameReversed && reversed.at(cellX, cellZ) == a.at(cellX, cellZ);
        }
    }
    for (int cellZ = -kHalf; cellZ < kHalf; ++cellZ) {
        for (int cellX = -kHalf; cellX < kHalf; ++cellX) {
            const CaveDistrict& district = a.at(cellX, cellZ);
            const CaveDistrict& another = other.at(cellX, cellZ);
            ++cells;
            same = same && district == b.at(cellX, cellZ);
            otherSites += district.siteX != another.siteX && district.siteZ != another.siteZ;
            otherTypes += district.type != another.type ? 1 : 0;
        }
    }
    bool sameAround = true;
    for (int n = 0; n < 2000; ++n) {
        const double x = -12000.0 + 24000.0 * hashUnit(n, 0, 11);
        const double z = -12000.0 + 24000.0 * hashUnit(n, 1, 11);
        sameAround = sameAround && sameWeights(a.weights(x, z), reversed.weights(x, z)) &&
                     a.profileAt(x, z) == b.profileAt(x, z);
    }
    std::printf("determinism: %d cells, %d with other sites and %d with other types with "
                "seed + 1\n",
                cells, otherSites, otherTypes);
    TEST_CHECK(same && sameReversed && sameAround);
    TEST_CHECK(otherSites * 10 >= cells * 9);
    TEST_CHECK(otherTypes * 10 >= cells);
}

// Sites lie in their own cell, within jitter / 2 of its center, so that the sites of two
// neighboring cells are 0.5 to 1.5 cells (1.5 to 4.5 km) apart along an axis; without jitter
// they are the centers of the cells. The cells are those of the grid of the world: about
// cellSize, a whole number of them along each side of the world.
void testSites() {
    const CaveDistrictParams params;
    World w(kSeed, params, variedSite());
    const double sizeX = w.districts().cellsX().size;
    const double sizeZ = w.districts().cellsZ().size;
    TEST_CHECK(std::abs(sizeX - params.cellSize) < 0.1 && std::abs(sizeZ - params.cellSize) < 0.2);
    constexpr int kHalf = 20;
    bool inCell = true;
    bool nearCenter = true;
    bool apart = true;
    double widest = 0.0;
    for (int cellZ = -kHalf; cellZ < kHalf; ++cellZ) {
        for (int cellX = -kHalf; cellX < kHalf; ++cellX) {
            const CaveDistrict& d = w.at(cellX, cellZ);
            const double offsetX = d.siteX / sizeX - cellX - 0.5;
            const double offsetZ = d.siteZ / sizeZ - cellZ - 0.5;
            inCell = inCell && d.cellX == cellX && d.cellZ == cellZ && std::abs(offsetX) < 0.5 &&
                     std::abs(offsetZ) < 0.5;
            nearCenter = nearCenter && std::abs(offsetX) <= params.jitter / 2.0 + 1.0e-9 &&
                         std::abs(offsetZ) <= params.jitter / 2.0 + 1.0e-9;
            widest = std::max({widest, std::abs(offsetX), std::abs(offsetZ)});
            const double gapX = w.at(cellX + 1, cellZ).siteX - d.siteX;
            const double gapZ = w.at(cellX, cellZ + 1).siteZ - d.siteZ;
            apart = apart && gapX >= 0.5 * sizeX - 1.0e-6 && gapX <= 1.5 * sizeX + 1.0e-6 &&
                    gapZ >= 0.5 * sizeZ - 1.0e-6 && gapZ <= 1.5 * sizeZ + 1.0e-6;
        }
    }
    std::printf("sites: widest offset from a center %.3f cells (jitter / 2 = %.3f)\n", widest,
                params.jitter / 2.0);
    TEST_CHECK(inCell && nearCenter && apart);
    TEST_CHECK(widest > 0.2); // the jitter is used, not only a tiny part of it

    CaveDistrictParams exact;
    exact.cellSize = 1000.0;
    exact.blend = 100.0;
    exact.jitter = 0.0;
    World centered(kSeed, exact, variedSite());
    const double exactX = centered.districts().cellsX().size;
    const double exactZ = centered.districts().cellsZ().size;
    bool centers = true;
    for (int cellZ = -kHalf; cellZ < kHalf; ++cellZ) {
        for (int cellX = -kHalf; cellX < kHalf; ++cellX) {
            const CaveDistrict& d = centered.at(cellX, cellZ);
            centers = centers && std::abs(d.siteX - (cellX + 0.5) * exactX) < 1.0e-9 &&
                      std::abs(d.siteZ - (cellZ + 0.5) * exactZ) < 1.0e-9;
        }
    }
    TEST_CHECK(centers);
}

bool constructorThrows(const CaveDistrictParams& params) {
    try {
        const CaveDistricts districts(kSeed, params);
        (void)districts;
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

// Narrower side of the cells of the grid of the world for a cell size.
double narrowestCell(double cellSize) {
    const terrain::WorldSize world;
    return std::min(terrain::periodicCells(world.x, cellSize).size,
                    terrain::periodicCells(world.z, cellSize).size);
}

// The constructor refuses a blend wider than 0.189 of the cell (the 3 x 3 cells around a point
// would no longer hold every district weighing on it) and a jitter above 0.5, and accepts both
// limits. The cell is the narrower side of the cells of the grid of the world.
void testConstructorLimits() {
    CaveDistrictParams params;
    TEST_CHECK(!constructorThrows(params));
    const double widest = 0.189 * narrowestCell(params.cellSize);
    params.blend = widest;
    TEST_CHECK(!constructorThrows(params));
    params.blend = std::nextafter(widest, 1.0e9);
    TEST_CHECK(constructorThrows(params));
    params.blend = 0.19 * params.cellSize;
    TEST_CHECK(constructorThrows(params));
    params.blend = params.cellSize;
    TEST_CHECK(constructorThrows(params));
    for (const double blend : {0.0, -10.0}) {
        params.blend = blend;
        TEST_CHECK(constructorThrows(params));
    }

    params = CaveDistrictParams{};
    for (const double jitter : {0.0, 0.25, 0.5}) {
        params.jitter = jitter;
        TEST_CHECK(!constructorThrows(params));
    }
    for (const double jitter : {std::nextafter(0.5, 1.0), 0.51, 1.0, -0.1}) {
        params.jitter = jitter;
        TEST_CHECK(constructorThrows(params));
    }

    // The limit follows the size of the cell.
    params = CaveDistrictParams{};
    params.cellSize = 1000.0;
    params.blend = 0.189 * narrowestCell(1000.0);
    TEST_CHECK(!constructorThrows(params));
    params.blend = 190.0;
    TEST_CHECK(constructorThrows(params));
}

// ---------------------------------------------------------------------------

// Cold sites (0 degrees included) draw glaciers and never volcanic or lush districts; warmer
// ones never draw glaciers.
void testColdAndWarm() {
    const TypeCounts cold = countTypes(constantSite({-10.0f, 0.7f, 50.0f}));
    const TypeCounts freezing = countTypes(constantSite({0.0f, 0.7f, 50.0f}));
    const TypeCounts cool = countTypes(constantSite({std::nextafter(0.0f, 1.0f), 0.7f, 50.0f}));
    const TypeCounts warm = countTypes(constantSite({25.0f, 0.7f, 50.0f}));
    std::printf("climates: glacier %d (cold), %d (0 degrees), %d (just above), %d (warm)\n",
                countOf(cold, CaveDistrictType::Glacier),
                countOf(freezing, CaveDistrictType::Glacier),
                countOf(cool, CaveDistrictType::Glacier), countOf(warm, CaveDistrictType::Glacier));
    for (const TypeCounts& counts : {cold, freezing}) {
        TEST_CHECK(countOf(counts, CaveDistrictType::Glacier) > 0);
        TEST_CHECK(countOf(counts, CaveDistrictType::Volcanic) == 0);
        TEST_CHECK(countOf(counts, CaveDistrictType::Lush) == 0);
    }
    for (const TypeCounts& counts : {cool, warm}) {
        TEST_CHECK(countOf(counts, CaveDistrictType::Glacier) == 0);
        TEST_CHECK(countOf(counts, CaveDistrictType::Volcanic) > 0);
    }
    TEST_CHECK(countOf(cool, CaveDistrictType::Lush) == 0);
    TEST_CHECK(countOf(warm, CaveDistrictType::Lush) > 0);
}

// Lush districts only where it is at least 10 degrees and the humidity at least 0.55.
void testLushNeedsWarmAndHumid() {
    const float justCool = std::nextafter(10.0f, 0.0f);
    const float justDry = std::nextafter(0.55f, 0.0f);
    for (const float temperature : {5.0f, justCool, 10.0f, 25.0f}) {
        for (const float humidity : {0.1f, justDry, 0.55f, 0.95f}) {
            const TypeCounts counts = countTypes(constantSite({temperature, humidity, 50.0f}));
            const bool mild = temperature >= 10.0f && humidity >= 0.55f;
            TEST_CHECK((countOf(counts, CaveDistrictType::Lush) > 0) == mild);
        }
    }
}

// Flooded districts only on ground lower than floodedMaxHeight at their site, whatever its value.
void testFloodedOnLowGround() {
    for (const float height : {-300.0f, 0.0f, 149.5f, 150.0f, 800.0f}) {
        const TypeCounts counts = countTypes(constantSite({12.0f, 0.5f, height}));
        TEST_CHECK((countOf(counts, CaveDistrictType::Flooded) > 0) == (height < 150.0f));
    }
    CaveDistrictParams high;
    high.floodedMaxHeight = 900.0f;
    TEST_CHECK(countOf(countTypes(constantSite({12.0f, 0.5f, 800.0f}), 30, high),
                       CaveDistrictType::Flooded) > 0);
    CaveDistrictParams low;
    low.floodedMaxHeight = -100.0f;
    const TypeCounts deep = countTypes(constantSite({12.0f, 0.5f, -150.0f}), 30, low);
    const TypeCounts shallow = countTypes(constantSite({12.0f, 0.5f, -50.0f}), 30, low);
    TEST_CHECK(countOf(deep, CaveDistrictType::Flooded) > 0);
    TEST_CHECK(countOf(shallow, CaveDistrictType::Flooded) == 0);
}

// Humidity favors lakes, rivers (from 0.6) and dripstone (from 0.45): over 3600 cells of the
// same temperature, a humid climate draws at least 1.3 times as many of the first two as a damp
// one, and a damp one at least twice as many dripstone districts as a dry one.
void testHumidityPreferences() {
    const TypeCounts humid = countTypes(constantSite({5.0f, 0.7f, 50.0f}));
    const TypeCounts damp = countTypes(constantSite({5.0f, 0.5f, 50.0f}));
    const TypeCounts dry = countTypes(constantSite({5.0f, 0.3f, 50.0f}));
    std::printf("humidity: lakes %d / %d, rivers %d / %d (humid / damp), dripstone %d / %d "
                "(damp / dry)\n",
                countOf(humid, CaveDistrictType::Lakes), countOf(damp, CaveDistrictType::Lakes),
                countOf(humid, CaveDistrictType::Rivers), countOf(damp, CaveDistrictType::Rivers),
                countOf(damp, CaveDistrictType::Dripstone),
                countOf(dry, CaveDistrictType::Dripstone));
    TEST_CHECK(10 * countOf(humid, CaveDistrictType::Lakes) >
               13 * countOf(damp, CaveDistrictType::Lakes));
    TEST_CHECK(10 * countOf(humid, CaveDistrictType::Rivers) >
               13 * countOf(damp, CaveDistrictType::Rivers));
    TEST_CHECK(countOf(damp, CaveDistrictType::Dripstone) >
               2 * countOf(dry, CaveDistrictType::Dripstone));
    for (const TypeCounts& counts : {humid, damp, dry}) {
        for (const CaveDistrictType type :
             {CaveDistrictType::Tunnels, CaveDistrictType::Halls, CaveDistrictType::Crevasses}) {
            TEST_CHECK(countOf(counts, type) > 0); // the types of every climate
        }
    }
}

// Over a varied climate every type is drawn, and each only where its rules allow it.
void testEveryTypeAppears() {
    const CaveDistrictParams params;
    const terrain::DistrictSite site = variedSite();
    World w(kSeed, params, site);
    constexpr int kHalf = 20;
    const TypeCounts counts = countTypes(w, kHalf);
    bool rulesHold = true;
    for (int cellZ = -kHalf; cellZ < kHalf; ++cellZ) {
        for (int cellX = -kHalf; cellX < kHalf; ++cellX) {
            const CaveDistrict& d = w.at(cellX, cellZ);
            CaveEnvironment environment;
            float height = 0.0f;
            site(d.siteX, d.siteZ, height, environment);
            rulesHold = rulesHold && allowed(d.type, environment, height, params.floodedMaxHeight);
        }
    }
    std::printf("varied climate, %d cells:", 4 * kHalf * kHalf);
    for (int type = 0; type < kTypeCount; ++type) {
        std::printf(" %s %d", terrain::caveDistrictName(static_cast<CaveDistrictType>(type)),
                    counts[static_cast<std::size_t>(type)]);
        TEST_CHECK(counts[static_cast<std::size_t>(type)] > 0);
    }
    std::printf("\n");
    TEST_CHECK(rulesHold);
}

// ---------------------------------------------------------------------------

// The default world has every type of district among the cells [-8, 8]^2 around the window,
// each on a site whose climate and height allow it; the field of the caves and its cache give the
// same districts.
void testRealWorld() {
    terrain::CaveParams caves;
    caves.districts = CaveDistrictParams{};
    const terrain::TerrainTiles tiles(kSeed, test_relief::legacyRelief(), {}, caves);
    const terrain::CaveGround& ground = tiles.caveGround();
    terrain::AquiferCache cache(*tiles.caves(), ground);
    const float floodedMax = caves.districts->floodedMaxHeight;
    TypeCounts counts{};
    bool rulesHold = true;
    bool sameAsField = true;
    for (int cellZ = kWindowCellZ - 20; cellZ <= kWindowCellZ + 20; ++cellZ) {
        for (int cellX = kWindowCellX - 20; cellX <= kWindowCellX + 20; ++cellX) {
            const CaveDistrict& d = cache.district(cellX, cellZ);
            const CaveEnvironment environment = ground.environment(d.siteX, d.siteZ);
            rulesHold = rulesHold &&
                        allowed(d.type, environment, ground.height(d.siteX, d.siteZ), floodedMax);
            sameAsField = sameAsField && d == tiles.caves()->district(cellX, cellZ, ground);
            if (std::abs(cellX - kWindowCellX) <= 8 && std::abs(cellZ - kWindowCellZ) <= 8) {
                ++counts[static_cast<std::size_t>(d.type)];
            }
        }
    }
    std::printf("real world, cells [-8, 8]^2 around (%d, %d):", kWindowCellX, kWindowCellZ);
    for (int type = 0; type < kTypeCount; ++type) {
        std::printf(" %s %d", terrain::caveDistrictName(static_cast<CaveDistrictType>(type)),
                    counts[static_cast<std::size_t>(type)]);
        TEST_CHECK(counts[static_cast<std::size_t>(type)] > 0);
    }
    std::printf("\n");
    TEST_CHECK(rulesHold && sameAsField);
}

// ---------------------------------------------------------------------------

// Every type has its own lowercase name.
void testNames() {
    std::set<std::string> names;
    bool lowercase = true;
    for (int type = 0; type < kTypeCount; ++type) {
        const std::string name = terrain::caveDistrictName(static_cast<CaveDistrictType>(type));
        names.insert(name);
        lowercase = lowercase && !name.empty() && std::all_of(name.begin(), name.end(), [](char c) {
                        return c >= 'a' && c <= 'z';
                    });
    }
    TEST_CHECK(names.size() == static_cast<std::size_t>(kTypeCount) && lowercase);
    TEST_CHECK(std::string(terrain::caveDistrictName(CaveDistrictType::Tunnels)) == "tunnels");
    TEST_CHECK(std::string(terrain::caveDistrictName(CaveDistrictType::Dripstone)) == "dripstone");
}

// The neutral profile changes nothing; the default profiles are distinct, change the caves, keep
// the [0, 1] fields in range, and only glacier is icy, lush lush and volcanic volcanic.
void testProfiles() {
    const CaveProfile neutral;
    using F = CaveProfile::Field;
    for (std::size_t f = 0; f < CaveProfile::kFieldCount; ++f) {
        const bool scale =
            f == F::kSpaghettiScale || f == F::kSpeleothemChance || f == F::kSpeleothemLength;
        TEST_CHECK(neutral.values[f] == (scale ? 1.0f : 0.0f));
    }
    TEST_CHECK(!neutral.icy() && !neutral.lush() && !neutral.volcanic());
    CaveProfile edge;
    edge[F::kIce] = edge[F::kLush] = edge[F::kVolcanic] = std::nextafter(0.5f, 0.0f);
    TEST_CHECK(!edge.icy() && !edge.lush() && !edge.volcanic());
    edge[F::kIce] = edge[F::kLush] = edge[F::kVolcanic] = 0.5f;
    TEST_CHECK(edge.icy() && edge.lush() && edge.volcanic());

    const CaveDistrictParams params;
    const auto profiles = terrain::defaultDistrictProfiles();
    TEST_CHECK(params.profiles == profiles);
    TEST_CHECK(params.cellSize == 3000.0 && params.jitter == 0.5 && params.blend == 280.0);
    TEST_CHECK(params.floodedMaxHeight == 150.0f);
    for (std::size_t a = 0; a < profiles.size(); ++a) {
        const auto type = static_cast<CaveDistrictType>(a);
        TEST_CHECK(profiles[a] != neutral);
        TEST_CHECK(profiles[a].icy() == (type == CaveDistrictType::Glacier));
        TEST_CHECK(profiles[a].lush() == (type == CaveDistrictType::Lush));
        TEST_CHECK(profiles[a].volcanic() == (type == CaveDistrictType::Volcanic));
        TEST_CHECK(profiles[a][F::kSpaghettiScale] > 0.0f);
        for (const F field : {F::kFlooded, F::kIce, F::kLush, F::kVolcanic}) {
            TEST_CHECK(profiles[a][field] >= 0.0f && profiles[a][field] <= 1.0f);
        }
        for (std::size_t b = a + 1; b < profiles.size(); ++b) {
            TEST_CHECK(profiles[a] != profiles[b]);
        }
    }
}

} // namespace

int main() {
    testDeterminism();
    testSites();
    testConstructorLimits();
    testColdAndWarm();
    testLushNeedsWarmAndHumid();
    testFloodedOnLowGround();
    testHumidityPreferences();
    testEveryTypeAppears();
    testRealWorld();
    testNames();
    testProfiles();
    return test::result();
}

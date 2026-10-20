// Lakes and entrances of the cave districts (Phase 8C-1): the entrances that would reach the water
// raised by the districts are dropped and the others stay dry, and the lakes of districts that
// blend keep the rules of the aquifers (see aquifers_test.cpp): no water as a wall against the air
// (the barriers close the caves along the bisectors between cells of different levels), none over
// the ground, over small districts and over the default ones at one of their borders. Aquifer
// cells, water volumes and flooded districts: cave_districts_water_test.cpp.
#include "terrain/generation/aquifers.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "aquifers_test_support.hpp"
#include "cave_districts_carving_support.hpp"
#include "caves_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <set>
#include <vector>

namespace {

using aquifers_test::clearanceOver;
using aquifers_test::climateGround;
using aquifers_test::countBarriers;
using aquifers_test::countLakes;
using aquifers_test::findBisectors;
using aquifers_test::LakeCounts;
using aquifers_test::latticeAround;
using aquifers_test::Scan;
using aquifers_test::waterInEntrance;
using aquifers_test::waterOver;
using caves_test::flatGround;
using caves_test::heightfieldGround;
using caves_test::humidEnvironment;
using caves_test::kSeed;
using caves_test::Lattice;
using caves_test::withEnvironment;
using districts_test::defaultCaves;
using districts_test::defaultProfile;
using districts_test::denseCaves;
using districts_test::dry;
using districts_test::uniformCaves;
using districts_test::withoutAquifers;
using terrain::AquiferCache;
using terrain::CaveDetail;
using terrain::CaveDistrictType;
using terrain::CaveEntrance;
using terrain::CaveField;
using terrain::CaveGround;
using terrain::CaveProfile;
using terrain::Heightfield;
using terrain::TerrainTiles;

constexpr float kFlatHeight = 300.0f;

// ---------------------------------------------------------------------------

using EntranceList = std::vector<CaveEntrance>;

bool listed(const EntranceList& list, const CaveEntrance& e) {
    return std::find(list.begin(), list.end(), e) != list.end();
}

constexpr double kEntranceReach = 1536.0;

EntranceList entrancesOf(const CaveField& field, const CaveGround& ground) {
    return field.entrancesNear(-kEntranceReach, -kEntranceReach, kEntranceReach, kEntranceReach,
                               ground);
}

// Entrances of the wetter districts, over humid flat ground, against the same candidates over
// dry ground: the ones kept are a subset of those kept by the neutral districts and clear the
// water and the barriers of the cells around them (levels raised by the districts included), the
// ones dropped all reach them, and nothing is left in a flooded district. Over the raised lakes
// the entrances kept stay dry.
void testEntrancesStayDry() {
    const CaveGround ground = climateGround(kFlatHeight, 12.0f, 0.9f);
    const CaveField bare(kSeed, dry(uniformCaves(CaveProfile{})));
    const CaveField neutral(kSeed, uniformCaves(CaveProfile{}));
    const EntranceList all = entrancesOf(bare, ground);
    const EntranceList base = entrancesOf(neutral, ground);
    const double margin = static_cast<double>(neutral.params().aquifers.entranceMargin);
    std::printf("entrances: %zu over dry ground, %zu with the neutral districts\n", all.size(),
                base.size());
    TEST_CHECK(all.size() >= 40 && base.size() < all.size());
    for (const CaveDistrictType type : {CaveDistrictType::Lakes, CaveDistrictType::Rivers,
                                        CaveDistrictType::Lush, CaveDistrictType::Flooded}) {
        const CaveField field(kSeed, uniformCaves(defaultProfile(type)));
        const EntranceList kept = entrancesOf(field, ground);
        AquiferCache cache(field, ground);
        bool subset = true;
        bool clear = true;
        std::size_t reached = 0;
        for (const CaveEntrance& e : kept) {
            subset = subset && listed(base, e);
            clear = clear && clearanceOver(e, cache) >= margin;
        }
        for (const CaveEntrance& e : all) {
            reached += !listed(kept, e) && clearanceOver(e, cache) < margin ? 1 : 0;
        }
        std::printf("  %-6s %3zu kept, %3zu dropped (%zu reach the water)\n",
                    terrain::caveDistrictName(type), kept.size(), all.size() - kept.size(),
                    reached);
        TEST_CHECK(subset && clear && reached == all.size() - kept.size());
        TEST_CHECK(kept.size() < base.size() || kept.empty());
        TEST_CHECK(type != CaveDistrictType::Flooded || kept.empty());
        if (type == CaveDistrictType::Lakes) {
            std::size_t inside = 0;
            std::size_t wet = 0;
            for (std::size_t n = 0; n < std::min<std::size_t>(kept.size(), 3); ++n) {
                const aquifers_test::EntranceWater water = waterInEntrance(field, ground, kept[n]);
                inside += water.inside;
                wet += water.wet;
            }
            TEST_CHECK(inside >= 1000 && wet == 0);
        }
    }
}

// ---------------------------------------------------------------------------

// Where the lattices of the lakes are taken: the bisectors nearest to a point, or spread over
// the range of cells.
struct Focus {
    bool used = false;
    double x = 0.0;
    double z = 0.0;
};

std::vector<aquifers_test::Bisector> pickBisectors(std::vector<aquifers_test::Bisector> sites,
                                                   const Focus& focus, std::size_t count) {
    std::vector<aquifers_test::Bisector> picked;
    if (focus.used) {
        std::stable_sort(sites.begin(), sites.end(), [&focus](const auto& a, const auto& b) {
            return std::hypot(a.x - focus.x, a.z - focus.z) <
                   std::hypot(b.x - focus.x, b.z - focus.z);
        });
        picked.assign(sites.begin(),
                      sites.begin() + static_cast<std::ptrdiff_t>(std::min(count, sites.size())));
        return picked;
    }
    const std::size_t stride = std::max<std::size_t>(1, sites.size() / count);
    for (std::size_t n = 0; n < sites.size() && picked.size() < count; n += stride) {
        picked.push_back(sites[n]);
    }
    return picked;
}

// The lakes of the lattices around some bisectors of aquifer cells, over cave districts that
// blend: the rules of the aquifers hold with the profiles of the districts, as without them (see
// aquifers_test.cpp): every sample of water lies in the cave air, under the level of its column
// and under the ground, the cave air under the level is water, dry columns hold none, no water
// stands as a wall against the air (the barriers close the caves along the bisectors, which the
// same caves without barriers would not), and the barriers only close the caves in their rock.
void checkLakes(const CaveField& field, const CaveGround& ground,
                const aquifers_test::CellRange& range, float minHeight, const Focus& focus,
                const char* name, std::size_t minTypes) {
    constexpr std::size_t kLattices = 8;
    const std::vector<aquifers_test::Bisector> picked =
        pickBisectors(findBisectors(field, ground, range, minHeight), focus, kLattices);
    const CaveDetail bare = withoutAquifers();
    AquiferCache districts(field, ground);
    LakeCounts lakes;
    LakeCounts unclosed;
    aquifers_test::BarrierCounts barriers;
    std::set<CaveDistrictType> types;
    std::size_t blended = 0;
    bool sameWater = true;
    for (const aquifers_test::Bisector& site : picked) {
        const Lattice lattice = latticeAround(site, 16, 24, 8);
        const Scan with(field, ground, CaveDetail{}, lattice);
        const Scan without(field, ground, bare, lattice);
        sameWater = sameWater && waterOver(field, with, with.carved()) == with.water();
        lakes.add(countLakes(field, with, with.carved(), with.water()));
        unclosed.add(
            countLakes(field, with, without.carved(), waterOver(field, with, without.carved())));
        barriers.add(countBarriers(field, with, without));
        const terrain::DistrictWeights weights = districts.districtWeights(site.x, site.z);
        types.insert(weights.districts[0].type);
        blended += weights.count > 1 ? 1 : 0;
    }
    std::printf("lakes (%s): %zu lattices in %zu district types (%zu blended), %zu water "
                "samples, %zu barrier samples close the caves, %zu water walls without barriers\n",
                name, picked.size(), types.size(), blended, lakes.water, barriers.closed,
                unclosed.walls);
    TEST_CHECK(picked.size() == kLattices && sameWater);
    TEST_CHECK(types.size() >= minTypes && blended >= 1);
    TEST_CHECK(lakes.water >= 500);
    TEST_CHECK(lakes.outsideAir == 0 && lakes.aboveLevel == 0 && lakes.aboveGround == 0);
    TEST_CHECK(lakes.dryAir == 0 && lakes.dryColumns == 0 && lakes.walls == 0);
    TEST_CHECK(barriers.closed >= 50);
    TEST_CHECK(barriers.strayed == 0 && barriers.open == 0 && barriers.changed == 0);
    TEST_CHECK(barriers.added == 0);
    TEST_CHECK(unclosed.walls > 0);
}

// The lakes over small districts (flat ground and relief, humid climate) and over the default
// ones, around the first of their borders over low ground whose 8 x 8 cells hold at least 8
// usable bisectors, taking the 8 nearest to it.
void testLakes() {
    const CaveField dense(kSeed, denseCaves());
    const Heightfield heightfield(kSeed);
    checkLakes(dense, withEnvironment(flatGround(100.0f), humidEnvironment()), {-6, -6, 12}, 0.0f,
               {}, "dense, flat", 3);
    checkLakes(dense, withEnvironment(heightfieldGround(heightfield), humidEnvironment()),
               {1, -14, 8}, 20.0f, {}, "dense, relief", 3);
    const TerrainTiles tiles(kSeed, {}, {}, defaultCaves());
    const CaveField& field = *tiles.caves();
    const CaveGround& ground = tiles.caveGround();
    const double size = field.params().aquifers.cellSize;
    for (const districts_test::Border& border :
         districts_test::bordersOf(field, ground, {-8, 17}, 20.0f, 400.0f)) {
        const aquifers_test::CellRange range{static_cast<int>(std::floor(border.x / size)) - 4,
                                             static_cast<int>(std::floor(border.z / size)) - 4, 8};
        if (findBisectors(field, ground, range, 20.0f).size() >= 8) {
            checkLakes(field, ground, range, 20.0f, {true, border.x, border.z},
                       "default districts at a border", 2);
            return;
        }
    }
    TEST_CHECK(false);
}

// ---------------------------------------------------------------------------

// True if a sample of water is where the aquifers and the rivers put it: the margin under the
// ground, in the cave air (within the skin of the rock), under the level of its column there.
bool wellPlaced(const CaveField& field, const terrain::CaveColumn& column, double y, float rock,
                float carved) {
    const terrain::AquiferParams& a = field.params().aquifers;
    return rock < -a.groundMargin && carved > -a.skin &&
           y < static_cast<double>(field.waterLevelAt(column, y));
}

// Around the sites of two districts of each wet type of the default world, on land, random points
// from 10 m over the ground to 100 m under it: all the water is well placed, so none is ever over
// the surface.
void testNoWaterOverTheGround() {
    const TerrainTiles tiles(kSeed, {}, {}, defaultCaves());
    const CaveField& field = *tiles.caves();
    const CaveGround& ground = tiles.caveGround();
    std::size_t points = 0;
    std::size_t water = 0;
    std::size_t misplaced = 0;
    caves_test::Rng rng(71);
    for (const CaveDistrictType type : {CaveDistrictType::Lakes, CaveDistrictType::Flooded,
                                        CaveDistrictType::Rivers, CaveDistrictType::Lush}) {
        const std::vector<terrain::CaveDistrict> found =
            districts_test::districtsOf(field, ground, {-8, 17}, type, 5.0f);
        TEST_CHECK(found.size() >= 2);
        for (std::size_t n = 0; n < std::min<std::size_t>(found.size(), 2); ++n) {
            const std::vector<caves_test::Point> sample = caves_test::randomPoints(
                ground, rng, 4000, {found[n].siteX, found[n].siteZ, 96.0}, -10.0, 100.0);
            terrain::CaveSampler sampler(field, ground, CaveDetail{}, caves_test::boxOf(sample));
            for (const caves_test::Point& p : sample) {
                const terrain::CaveColumn column = sampler.column(p.x, p.z, p.ground);
                const float rock = caves_test::terrainDensity(p.y, p.ground);
                const float carved = sampler.density(column, p.y, rock);
                if (sampler.waterDensity(column, p.y, rock, carved) < 0.0f) {
                    ++water;
                    misplaced += wellPlaced(field, column, p.y, rock, carved) ? 0 : 1;
                }
                ++points;
            }
        }
    }
    std::printf("water over the ground: %zu points around 8 districts, %zu with water, %zu "
                "misplaced\n",
                points, water, misplaced);
    TEST_CHECK(water >= 300 && misplaced == 0);
}

} // namespace

int main() {
    testEntrancesStayDry();
    testLakes();
    testNoWaterOverTheGround();
    return test::result();
}

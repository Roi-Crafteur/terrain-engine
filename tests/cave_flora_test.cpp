// Cave flora of the lush cave regions (cave_flora.hpp): deterministic placement from any sampler,
// adjacent squares giving their union, no plant without districts or without a lush profile, the
// streamed tiles unchanged by the flora (only TileData::caveFlora differs), the placement checked
// against the carved density (ferns and mushrooms on floors, vines under ceilings, air along the
// plant, above the water, within the share of the free height, outside the entrances, never in ice
// or over a frozen lake), and the parameters read from the field. The lush districts are those of
// the default world (seed 1337); caves whose every district is lush control the climate, the water,
// the ice and the entrances.
#include "terrain/generation/cave_flora.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/streaming/cave_tiles.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "cave_flora_test_support.hpp"
#include "legacy_relief_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <utility>
#include <vector>

namespace {

using flora_test::around;
using flora_test::checkAll;
using flora_test::contains;
using flora_test::districtParams;
using flora_test::flatWorld;
using flora_test::kLushSites;
using flora_test::kSeed;
using flora_test::lushWorld;
using flora_test::mildEnvironment;
using flora_test::placeIn;
using flora_test::PlantChecks;
using flora_test::plantsOver;
using flora_test::samplerOver;
using flora_test::Site;
using flora_test::sorted;
using flora_test::Square;
using flora_test::subsetOf;
using terrain::CaveField;
using terrain::CaveFlora;
using terrain::CaveFloraField;
using terrain::CaveFloraKind;
using terrain::CaveGround;
using terrain::CaveParams;
using terrain::CaveProfile;
using terrain::CaveSampler;
using terrain::TerrainTiles;
using terrain::TileData;

constexpr double kSide = 80.0;

std::vector<CaveFlora> concat(std::vector<CaveFlora> a, const std::vector<CaveFlora>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

// ---------------------------------------------------------------------------

// The same seed, parameters and caves give the same plants from any sampler (of the square or of
// a wider one), in cell order; another seed gives other plants.
void testDeterminism(const TerrainTiles& tiles) {
    const CaveField& field = *tiles.caves();
    const CaveGround& ground = tiles.caveGround();
    const terrain::CaveFloraParams& params = field.districts()->params().flora;
    for (const Site& site : {kLushSites[2], kLushSites[3]}) {
        const Square square = around(site.x, site.z, kSide);
        const std::vector<CaveFlora> first = plantsOver(field, ground, square);
        CaveSampler wide = samplerOver(field, ground, around(site.x, site.z, 2.0 * kSide));
        const std::vector<CaveFlora> fromWide =
            placeIn(CaveFloraField(kSeed, params), wide, square);
        const std::vector<CaveFlora> other = plantsOver(field, ground, square, kSeed + 1);
        bool ordered = true;
        const auto cell = [&params](const CaveFlora& p) {
            return std::make_pair(std::floor(p.z / params.spacing),
                                  std::floor(p.x / params.spacing));
        };
        for (std::size_t n = 1; n < first.size(); ++n) {
            ordered = ordered && cell(first[n - 1]) <= cell(first[n]);
        }
        std::printf("determinism at (%.0f, %.0f): %zu plants, %zu with seed + 1\n", site.x, site.z,
                    first.size(), other.size());
        TEST_CHECK(first.size() > 20);
        TEST_CHECK(first == plantsOver(field, ground, square));
        TEST_CHECK(first == fromWide);
        TEST_CHECK(ordered);
        TEST_CHECK(other.size() > 20 && other != first);
    }
}

// Adjacent squares give the plants of their union (as multisets), each plant in its square:
// four squares of 40 m, each from a sampler of its own like the tiles, against the square of 80
// m, and two cuts of a sampler of the whole square that do not follow the grid of the plants.
void testUnion(const TerrainTiles& tiles) {
    const CaveField& field = *tiles.caves();
    const CaveGround& ground = tiles.caveGround();
    const terrain::CaveFloraParams& params = field.districts()->params().flora;
    const CaveFloraField flora(kSeed, params);
    int cut = 0;
    for (const Site& site : kLushSites) {
        const Square whole = around(site.x, site.z, kSide);
        const std::vector<CaveFlora> all = plantsOver(field, ground, whole);
        std::vector<CaveFlora> quarters;
        bool inside = true;
        for (const double dz : {0.0, 40.0}) {
            for (const double dx : {0.0, 40.0}) {
                const Square part{whole.minX + dx, whole.minZ + dz, 40.0};
                const std::vector<CaveFlora> plants = plantsOver(field, ground, part);
                inside =
                    inside && std::all_of(plants.begin(), plants.end(), [&part](const auto& p) {
                        return p.x >= part.minX && p.x < part.minX + 40.0 && p.z >= part.minZ &&
                               p.z < part.minZ + 40.0;
                    });
                quarters = concat(std::move(quarters), plants);
            }
        }
        CaveSampler sampler = samplerOver(field, ground, whole);
        const double cutX = whole.minX + 37.3;
        const double cutZ = whole.minZ + 41.9;
        const double maxX = whole.minX + kSide;
        const double maxZ = whole.minZ + kSide;
        const std::vector<CaveFlora> left =
            flora.place(sampler, whole.minX, whole.minZ, cutX, maxZ);
        const std::vector<CaveFlora> right = flora.place(sampler, cutX, whole.minZ, maxX, maxZ);
        const std::vector<CaveFlora> below =
            flora.place(sampler, whole.minX, whole.minZ, maxX, cutZ);
        const std::vector<CaveFlora> above = flora.place(sampler, whole.minX, cutZ, maxX, maxZ);
        std::printf("union at (%.0f, %.0f): %zu plants, %zu + %zu in two cuts\n", site.x, site.z,
                    all.size(), left.size(), right.size());
        TEST_CHECK(all.size() > 15 && inside);
        TEST_CHECK(sorted(quarters) == sorted(all));
        TEST_CHECK(sorted(concat(left, right)) == sorted(all));
        TEST_CHECK(sorted(concat(below, above)) == sorted(all));
        cut += !left.empty() && !right.empty() && !below.empty() && !above.empty() ? 1 : 0;
    }
    TEST_CHECK(cut >= 2); // the cuts do split the plants of most sites
}

// A square is open at its upper sides: a plant on the edge belongs to the square that starts at
// it, and not to the one that ends there.
void testHalfOpenSquares(const TerrainTiles& tiles) {
    const CaveField& field = *tiles.caves();
    const CaveFloraField flora(kSeed, field.districts()->params().flora);
    const Square whole = around(kLushSites[2].x, kLushSites[2].z, kSide);
    CaveSampler sampler = samplerOver(field, tiles.caveGround(), whole);
    const double maxX = whole.minX + kSide;
    const double maxZ = whole.minZ + kSide;
    const std::vector<CaveFlora> all = flora.place(sampler, whole.minX, whole.minZ, maxX, maxZ);
    TEST_CHECK(all.size() > 20);
    int edges = 0;
    for (std::size_t n = 3; n < all.size(); n += 7) {
        const CaveFlora& p = all[n];
        const auto placed = [&](double x0, double z0, double x1, double z1) {
            return contains(flora.place(sampler, x0, z0, x1, z1), p);
        };
        TEST_CHECK(!placed(whole.minX, whole.minZ, p.x, maxZ));
        TEST_CHECK(placed(p.x, whole.minZ, maxX, maxZ));
        TEST_CHECK(!placed(whole.minX, whole.minZ, maxX, p.z));
        TEST_CHECK(placed(whole.minX, p.z, maxX, maxZ));
        TEST_CHECK(!placed(p.x, p.z, p.x, p.z)); // a square without area holds nothing
        ++edges;
    }
    TEST_CHECK(edges >= 8);
}

// ---------------------------------------------------------------------------

// No plant without cave regions, none where no profile is lush (every lush field 0) and none
// with a chance of 0, in the square of a lush district that holds many.
void testNoPlantsWithoutLushRegions(const TerrainTiles& tiles) {
    const CaveGround& ground = tiles.caveGround();
    const Square square = around(kLushSites[2].x, kLushSites[2].z, kSide);
    TEST_CHECK(plantsOver(*tiles.caves(), ground, square).size() > 20);
    const CaveField plain(kSeed);
    TEST_CHECK(plain.districts() == nullptr);
    TEST_CHECK(plantsOver(plain, ground, square).empty());

    CaveParams dark = districtParams();
    for (CaveProfile& profile : dark.districts->profiles) {
        profile[CaveProfile::kLush] = 0.0f;
    }
    TEST_CHECK(plantsOver(CaveField(kSeed, dark), ground, square).empty());

    CaveParams barren = districtParams();
    barren.districts->flora.chance = 0.0f;
    TEST_CHECK(plantsOver(CaveField(kSeed, barren), ground, square).empty());
}

// The flora never changes the geometry: each LOD 0 tile of 5 x 5 around a lush district, with the
// default flora and with a chance of 0, only differs in caveFlora, which holds the placement over
// the square of the tile (and none on the coarser LODs).
void testGeometryUnchanged(const TerrainTiles& tiles) {
    CaveParams quietParams = districtParams();
    quietParams.districts->flora.chance = 0.0f;
    const TerrainTiles quiet(kSeed, test_relief::legacyRelief(), {}, quietParams);
    const terrain::ChunkCoord center = terrain::worldToChunk(kLushSites[2].x, kLushSites[2].z);
    const double side = terrain::tileSizeMeters(0);
    std::size_t planted = 0;      // tiles with plants
    std::size_t plantedWater = 0; // of them, tiles with a lake
    std::size_t plants = 0;
    std::size_t speleothems = 0;
    bool sameGeometry = true;
    bool placement = true;
    for (int dz = -2; dz <= 2; ++dz) {
        for (int dx = -2; dx <= 2; ++dx) {
            const terrain::ChunkCoord chunk{center.x + dx, center.z + dz};
            const terrain::TileRequest request{terrain::tileContaining(chunk, 0), 0};
            const TileData with = tiles.generate(request);
            const TileData without = quiet.generate(request);
            TileData stripped = with;
            stripped.caveFlora.clear();
            const Square tile{with.originX, with.originZ, side};
            sameGeometry = sameGeometry && stripped == without && without.caveFlora.empty() &&
                           !with.mesh.vertices.empty() &&
                           with.materials.size() == with.mesh.vertices.size();
            placement =
                placement && with.caveFlora == plantsOver(*tiles.caves(), tiles.caveGround(), tile);
            planted += with.caveFlora.empty() ? 0 : 1;
            plantedWater += with.caveFlora.empty() || with.water.vertices.empty() ? 0 : 1;
            plants += with.caveFlora.size();
            speleothems += with.speleothems.size();
        }
    }
    std::printf("tiles: %zu plants over %zu tiles (%zu with a lake), %zu speleothems\n", plants,
                planted, plantedWater, speleothems);
    TEST_CHECK(sameGeometry && placement);
    TEST_CHECK(planted >= 8 && plantedWater >= 5 && plants >= 60 && speleothems >= 5);
    for (int lod = 1; lod < terrain::kLodCount; ++lod) {
        TEST_CHECK(tiles.generate({terrain::tileContaining(center, lod), 0}).caveFlora.empty());
    }
}

// ---------------------------------------------------------------------------

void report(const char* name, const PlantChecks& c) {
    std::printf("%s: %zu plants (%zu ferns, %zu mushrooms, %zu vines), %zu over a lake, tightest "
                "share of the free height %.3f\n",
                name, c.plants, c.ferns, c.mushrooms, c.vines, c.wet, c.tightest);
    TEST_CHECK(c.badBase == 0);
    TEST_CHECK(c.offBase * 50 <= c.plants); // at most 2 % of the bases further than 0.3 m
    TEST_CHECK(c.badAir == 0);
    TEST_CHECK(c.badSize == 0);
    TEST_CHECK(c.badShare == 0);
    TEST_CHECK(c.underWater == 0);
    TEST_CHECK(c.notLush == 0);
    TEST_CHECK(c.icy == 0);
    TEST_CHECK(c.frozen == 0);
    TEST_CHECK(c.inEntrance == 0);
}

// The plants of the lush districts of the world, all over lakes under the sea: every fern and
// mushroom stands on a floor above the water, every vine hangs from a ceiling above it, in air
// all along, within the share of the free height.
void testPlacementInTheWorld(const TerrainTiles& tiles) {
    PlantChecks total;
    for (const Site& site : kLushSites) {
        const Square square = around(site.x, site.z, kSide);
        CaveSampler sampler = samplerOver(*tiles.caves(), tiles.caveGround(), square);
        const PlantChecks checks = checkAll(
            sampler, placeIn(CaveFloraField(kSeed, tiles.caves()->districts()->params().flora),
                             sampler, square));
        total.plants += checks.plants;
        total.ferns += checks.ferns;
        total.mushrooms += checks.mushrooms;
        total.vines += checks.vines;
        total.wet += checks.wet;
        total.tightest = std::max(total.tightest, checks.tightest);
        report("lush site", checks);
    }
    TEST_CHECK(total.plants > 100);
    TEST_CHECK(total.ferns > 20 && total.mushrooms > 10 && total.vines > 20);
    TEST_CHECK(total.wet == total.plants);
    TEST_CHECK(total.tightest > 0.9); // some plants are held by the share of the free height
}

// The square of 96 m between the opening and the chamber of the first entrance near the origin.
Square aroundFirstEntrance(const CaveField& field, const CaveGround& ground) {
    const std::vector<terrain::CaveEntrance> entrances =
        field.entrancesNear(-2048.0, -2048.0, 2048.0, 2048.0, ground);
    TEST_CHECK(!entrances.empty());
    const terrain::CaveEntrance& e = entrances.empty() ? terrain::CaveEntrance{} : entrances[0];
    return around((e.topX + e.bottomX) / 2.0, (e.topZ + e.bottomZ) / 2.0, 96.0);
}

// The same checks where every district is lush, over a flat mild ground with a single entrance:
// dry caves (floors, ceilings) and caves full of water (ferns and mushrooms above the lake,
// vines hanging over it).
void testPlacementInLushWorlds() {
    const CaveGround ground = flatWorld(mildEnvironment());
    for (const bool wet : {false, true}) {
        const CaveField field(kSeed, lushWorld(wet));
        const Square square = aroundFirstEntrance(field, ground);
        CaveSampler sampler = samplerOver(field, ground, square);
        const PlantChecks checks =
            checkAll(sampler, placeIn(CaveFloraField(kSeed), sampler, square));
        report(wet ? "lush world, wet" : "lush world, dry", checks);
        TEST_CHECK(!sampler.entrances().empty());
        TEST_CHECK(checks.plants > 30);
        TEST_CHECK(checks.ferns > 5 && checks.mushrooms > 2 && checks.vines > 5);
        TEST_CHECK(wet ? checks.wet == checks.plants : checks.wet == 0);
    }
}

// No plant in the column of an entrance (tube or chamber, widened by entranceMargin): with a
// wider margin the plants are those of the default margin, less the ones in the wider ring.
void testEntranceMargin() {
    const CaveGround ground = flatWorld(mildEnvironment());
    CaveParams narrow = lushWorld(false);
    CaveParams wide = lushWorld(false);
    wide.districts->flora.entranceMargin = 6.0f;
    const CaveField narrowField(kSeed, narrow);
    const CaveField wideField(kSeed, wide);
    const Square square = aroundFirstEntrance(narrowField, ground);
    CaveSampler sampler = samplerOver(narrowField, ground, square);
    const std::vector<CaveFlora> inNarrow = plantsOver(narrowField, ground, square);
    const std::vector<CaveFlora> inWide = plantsOver(wideField, ground, square);
    const auto near = [&sampler](const CaveFlora& p, double margin) {
        const auto& entrances = sampler.entrances();
        return std::any_of(entrances.begin(), entrances.end(), [&](const terrain::CaveEntrance& e) {
            return terrain::entranceColumn(e, p.x, p.z, margin);
        });
    };
    std::size_t ring = 0;
    bool ringIsWide = true;
    for (const CaveFlora& plant : inNarrow) {
        if (!contains(inWide, plant)) {
            ++ring;
            ringIsWide = ringIsWide && near(plant, 6.0) && !near(plant, 0.0);
        }
    }
    std::printf(
        "entrance margin: %zu plants with a margin of 1 m, %zu with 6 m (%zu in the ring)\n",
        inNarrow.size(), inWide.size(), ring);
    TEST_CHECK(inNarrow.size() > 30);
    TEST_CHECK(subsetOf(inWide, inNarrow));
    TEST_CHECK(ring > 0 && ringIsWide);
    TEST_CHECK(std::none_of(inNarrow.begin(), inNarrow.end(),
                            [&near](const CaveFlora& p) { return near(p, 1.0); }));
    TEST_CHECK(std::none_of(inWide.begin(), inWide.end(),
                            [&near](const CaveFlora& p) { return near(p, 6.0); }));
}

// ---------------------------------------------------------------------------

// The plants of the square of caves whose every district has the profile of lushWorld.
std::vector<CaveFlora> lushPlants(const CaveParams& params, const CaveGround& ground,
                                  const Square& square) {
    return plantsOver(CaveField(kSeed, params), ground, square);
}

// No plant in an icy profile (ice from 0.5, wet or dry caves) nor over a frozen lake (cold
// surface above a wet cave), and the same caves without ice or frost have plenty: below 0.5 the
// ice changes nothing, and a cold climate without lakes has nothing to freeze.
void testIceAndFrozenLakes() {
    const Square square = around(500.0, 500.0, kSide);
    const CaveGround mild = flatWorld(mildEnvironment());
    const CaveGround cold = flatWorld(caves_test::coldEnvironment());
    const std::vector<CaveFlora> reference = lushPlants(lushWorld(true), mild, square);
    TEST_CHECK(reference.size() > 30);
    TEST_CHECK(lushPlants(lushWorld(true, 1.0f, 0.49f), mild, square) == reference);
    TEST_CHECK(lushPlants(lushWorld(true, 1.0f, 0.5f), mild, square).empty());
    TEST_CHECK(lushPlants(lushWorld(false, 1.0f, 1.0f), mild, square).empty());

    const CaveField frozen(kSeed, lushWorld(true));
    CaveSampler sampler = samplerOver(frozen, cold, square);
    const CaveProfile profile = sampler.profileAt(square.minX, square.minZ);
    TEST_CHECK(sampler.column(square.minX, square.minZ, 300.0f).frozen && !profile.icy());
    TEST_CHECK(lushPlants(lushWorld(true), cold, square).empty());
    TEST_CHECK(lushPlants(lushWorld(false), cold, square).size() > 30);
}

// The lush field of the profile scales the chance (the plants of a lower field are some of those
// of a higher one, in proportion, none at 0) and every parameter is read from the field: chance,
// sizes and spacing.
void testScaling() {
    const Square square = around(500.0, 500.0, kSide);
    const CaveGround ground = flatWorld(mildEnvironment());
    const std::vector<CaveFlora> full = lushPlants(lushWorld(false), ground, square);
    const std::vector<CaveFlora> half = lushPlants(lushWorld(false, 0.5f), ground, square);
    const std::vector<CaveFlora> quarter = lushPlants(lushWorld(false, 0.25f), ground, square);
    const std::size_t count = full.size();
    std::printf("scaling: %zu plants, %zu at a lush field of 0.5, %zu at 0.25\n", count,
                half.size(), quarter.size());
    TEST_CHECK(count > 60);
    TEST_CHECK(subsetOf(quarter, half) && subsetOf(half, full));
    TEST_CHECK(half.size() * 100 > count * 35 && half.size() * 100 < count * 65);
    TEST_CHECK(quarter.size() * 100 > count * 15 && quarter.size() * 100 < count * 35);
    TEST_CHECK(lushPlants(lushWorld(false, 0.0f), ground, square).empty());

    CaveParams params = lushWorld(false);
    params.districts->flora.chance = 0.3f; // against 0.6
    const std::vector<CaveFlora> rarer = lushPlants(params, ground, square);
    params.districts->flora.chance = 1.0f;
    const std::vector<CaveFlora> denser = lushPlants(params, ground, square);
    TEST_CHECK(subsetOf(rarer, full) && subsetOf(full, denser));
    TEST_CHECK(rarer.size() * 100 > count * 35 && rarer.size() * 100 < count * 65);
    TEST_CHECK(denser.size() > count);

    params = lushWorld(false);
    params.districts->flora.minSize = 1.0f;
    params.districts->flora.maxSize = 1.0f;
    const std::vector<CaveFlora> fixed = lushPlants(params, ground, square);
    TEST_CHECK(!fixed.empty());
    TEST_CHECK(
        std::all_of(fixed.begin(), fixed.end(), [](const CaveFlora& p) { return p.size == 1.0f; }));

    params = lushWorld(false);
    params.districts->flora.spacing = 6.0;
    const std::size_t sparse = lushPlants(params, ground, square).size();
    params.districts->flora.spacing = 1.5;
    const std::size_t crowded = lushPlants(params, ground, square).size();
    TEST_CHECK(sparse * 100 < count * 40 && crowded > count * 2);
}

} // namespace

int main() {
    const TerrainTiles tiles(kSeed, test_relief::legacyRelief(), {}, districtParams());
    testDeterminism(tiles);
    testUnion(tiles);
    testHalfOpenSquares(tiles);
    testNoPlantsWithoutLushRegions(tiles);
    testGeometryUnchanged(tiles);
    testPlacementInTheWorld(tiles);
    testPlacementInLushWorlds();
    testEntranceMargin();
    testIceAndFrozenLakes();
    testScaling();
    return test::result();
}

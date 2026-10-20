// Caves of a field seen from a sampler: determinism, every component present, nothing above
// the surface, the roof and the depth limits, the deep sea and the rivers keeping the caves
// away, continuity, the details of the LODs, and the carving itself. Entrances:
// cave_entrances_test.cpp; caves under a region with rivers: cave_region_test.cpp. Every
// parameter is read from the field, nothing is hard-coded.
#include "terrain/generation/cave_entrances.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/streaming/cave_tiles.hpp"

#include "caves_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using caves_test::Area;
using caves_test::areaAroundEntrance;
using caves_test::boxOf;
using caves_test::carvedAir;
using caves_test::carvedAt;
using caves_test::findRavine;
using caves_test::flatGround;
using caves_test::heightfieldGround;
using caves_test::kSeed;
using caves_test::Point;
using caves_test::pointNearEntrance;
using caves_test::randomPoints;
using caves_test::Rng;
using caves_test::scanColumns;
using caves_test::terrainDensity;
using caves_test::Window;
using caves_test::withStraightRiver;
using terrain::CaveBox;
using terrain::CaveColumn;
using terrain::CaveDetail;
using terrain::CaveEntrance;
using terrain::CaveField;
using terrain::CaveGround;
using terrain::CaveParams;
using terrain::CaveSampler;
using terrain::CaveVoids;
using terrain::Heightfield;

constexpr float kFlatHeight = 300.0f;

// ---------------------------------------------------------------------------

// Same seed, same caves, from any sampler and from a point alone, whatever cells the sampler
// skips; another seed, other caves.
void testDeterminism() {
    const Heightfield heightfield(kSeed);
    const CaveGround ground = heightfieldGround(heightfield);
    Rng rng(1);
    const std::vector<Point> points =
        randomPoints(ground, rng, 2000, {1000.0, -2000.0, 256.0}, -5.0, 110.0);
    const CaveBox box = boxOf(points);
    const CaveBox wide{box.minX - 37.0, box.minY - 11.0, box.minZ - 53.0,
                       box.maxX + 29.0, box.maxY + 17.0, box.maxZ + 41.0};
    const CaveField a(kSeed);
    const CaveField b(kSeed);
    const CaveField c(kSeed + 1);
    const CaveDetail detail;
    CaveSampler sa(a, ground, detail, box);
    CaveSampler sb(b, ground, detail, wide);
    CaveSampler sc(c, ground, detail, box);
    bool same = true;
    int carved = 0;
    int differing = 0;
    for (const Point& p : points) {
        const CaveColumn ca = sa.column(p.x, p.z, p.ground);
        const CaveColumn cb = sb.column(p.x, p.z, p.ground);
        const float terrain = terrainDensity(p.y, p.ground);
        const float density = sa.density(ca, p.y, terrain);
        const float caveVoid = sa.voidAt(ca, p.y);
        same = same && density == sb.density(cb, p.y, terrain) &&
               density == caveDensityAt(a, ground, detail, p.x, p.y, p.z) &&
               caveVoid == sb.voidAt(cb, p.y) && density == a.carve(terrain, caveVoid, detail);
        if (density != terrain) {
            ++carved;
            differing += carvedAt(sc, p) != density ? 1 : 0;
        }
    }
    std::printf("determinism: %zu points, %d touched by the caves, %d of them differ with "
                "seed + 1\n",
                points.size(), carved, differing);
    TEST_CHECK(same);
    TEST_CHECK(carved >= 100);
    TEST_CHECK(differing * 5 >= carved * 4);
}

// Index (0 spaghetti, 1 noodles, 2 cheese, 3 ravines) of the smallest void of the four
// components if it carves within the limits, -1 otherwise.
int carvingComponent(const CaveVoids& v) {
    const std::array<float, 4> voids{v.spaghetti, v.noodles, v.cheese, v.ravines};
    const auto smallest = std::min_element(voids.begin(), voids.end());
    return std::max(*smallest, v.limit) < 0.0f ? static_cast<int>(smallest - voids.begin()) : -1;
}

// Every component exists: samples of carved air where each of them is the smallest void.
void testEveryComponentExists() {
    const CaveField field(kSeed);
    const CaveGround ground = flatGround(kFlatHeight);
    const int roof = static_cast<int>(field.params().minRoof);
    const int deepest = static_cast<int>(field.params().maxDepth);
    std::array<int, 4> counts{};
    bool skipsAreExact = true;
    const auto count = [&](CaveSampler& sampler, const CaveColumn& column, double y, float terrain,
                           float carved) {
        // The density the sampler gives without the interpolation it skips.
        skipsAreExact = skipsAreExact &&
                        carved == field.carve(terrain, sampler.voidAt(column, y), sampler.detail());
        if (carvedAir(terrain, carved)) {
            const int component = carvingComponent(sampler.voids(column, y));
            counts[static_cast<std::size_t>(std::max(component, 0))] += component >= 0 ? 1 : 0;
        }
    };
    scanColumns(field, ground, {0.0, 0.0, 256.0, 256.0, 8.0, roof, deepest}, count);
    double siteX = 0.0;
    double siteZ = 0.0;
    TEST_CHECK(findRavine(field, ground, kFlatHeight, siteX, siteZ));
    scanColumns(field, ground, {siteX - 4.0, siteZ - 4.0, 8.0, 8.0, 1.0, roof, deepest}, count);
    std::printf("components: carved air with spaghetti %d, noodles %d, cheese %d, ravines %d "
                "(ravine at %.0f, %.0f)\n",
                counts[0], counts[1], counts[2], counts[3], siteX, siteZ);
    for (const int n : counts) {
        TEST_CHECK(n >= 20);
    }
    TEST_CHECK(skipsAreExact);
}

// Random points above the surface and just under it, and around the entrances: the caves
// never add rock anywhere, and leave the density exactly alone from the surface down to half
// the roof outside the reach of the entrances.
void checkSurface(const CaveField& field, const CaveGround& ground, std::uint64_t stream,
                  const char* name) {
    const CaveParams& params = field.params();
    const double limitDepth = static_cast<double>(params.minRoof - params.blend) / 2.0;
    Rng rng(stream);
    const Area area = areaAroundEntrance(field, ground, 1000.0, -2000.0, 256.0);
    std::vector<Point> points = randomPoints(ground, rng, 1500, area, -20.0, limitDepth);
    for (const Point& p : randomPoints(ground, rng, 500, area, -20.0, 110.0)) {
        points.push_back(p);
    }
    const std::vector<CaveEntrance> entrances = field.entrancesNear(
        area.x - area.half, area.z - area.half, area.x + area.half, area.z + area.half, ground);
    for (const CaveEntrance& e : entrances) {
        for (int k = 0; k < 150; ++k) {
            points.push_back(pointNearEntrance(ground, rng, e, field.entranceReach(e)));
        }
    }
    CaveSampler sampler(field, ground, CaveDetail{}, boxOf(points));
    int nearEntrance = 0;
    int touched = 0;
    bool exact = true;
    bool neverAdds = true;
    for (const Point& p : points) {
        const float terrain = terrainDensity(p.y, p.ground);
        const float carved = carvedAt(sampler, p);
        const bool reached = CaveField::nearEntrance(p.x, p.z, sampler.entrances(),
                                                     terrain::kEntranceReachMargin);
        const bool deeper = static_cast<double>(p.ground) - p.y > limitDepth;
        nearEntrance += reached ? 1 : 0;
        touched += carved != terrain ? 1 : 0;
        neverAdds = neverAdds && carved >= terrain;
        exact = exact && (reached || deeper || carved == terrain);
    }
    std::printf("surface (%s): %zu points, %d near entrances, %d touched by the caves\n", name,
                points.size(), nearEntrance, touched);
    TEST_CHECK(exact);
    TEST_CHECK(neverAdds);
    TEST_CHECK(nearEntrance > 0 && touched > 0);
}

// No caves above the surface and no rock added, on flat ground and on the heightfield.
void testNothingAboveSurface() {
    const CaveField field(kSeed);
    const Heightfield heightfield(kSeed);
    checkSurface(field, flatGround(kFlatHeight), 2, "flat");
    checkSurface(field, heightfieldGround(heightfield), 3, "relief");
}

struct DepthStats {
    int columns = 0;
    int cavedColumns = 0;
    int tooShallow = 0;
    int tooDeep = 0;
    double shallowest = 1.0e9;
};

// Scans full columns outside the reach of the entrances: where the carved air lies in depth.
DepthStats scanDepths(const CaveField& field, const CaveGround& ground, const Window& window) {
    DepthStats stats;
    const CaveParams& params = field.params();
    const double roof = static_cast<double>(params.minRoof - params.blend / 4.0f);
    const double deepest = static_cast<double>(params.maxDepth + params.blend / 4.0f);
    const std::vector<CaveEntrance> entrances = field.entrancesNear(
        window.minX, window.minZ, window.minX + window.sizeX, window.minZ + window.sizeZ, ground);
    double lastX = 0.0;
    double lastZ = 0.0;
    bool caved = false;
    scanColumns(field, ground, window,
                [&](CaveSampler&, const CaveColumn& column, double y, float terrain, float carved) {
                    if (CaveField::nearEntrance(column.x, column.z, entrances,
                                                terrain::kEntranceReachMargin)) {
                        return;
                    }
                    if (stats.columns == 0 || lastX != column.x || lastZ != column.z) {
                        stats.cavedColumns += caved ? 1 : 0;
                        ++stats.columns;
                        caved = false;
                        lastX = column.x;
                        lastZ = column.z;
                    }
                    if (carvedAir(terrain, carved)) {
                        const double depth = static_cast<double>(column.ground) - y;
                        caved = true;
                        stats.tooShallow += depth < roof ? 1 : 0;
                        stats.tooDeep += depth > deepest ? 1 : 0;
                        stats.shallowest = std::min(stats.shallowest, depth);
                    }
                });
    stats.cavedColumns += caved ? 1 : 0;
    return stats;
}

// Caves stay between the minimum roof and the maximum depth below their column, a ravine
// wide open at the top included.
void testRoofThickness() {
    const CaveField field(kSeed);
    const Heightfield heightfield(kSeed);
    const CaveParams& params = field.params();
    const int bottom = static_cast<int>(params.maxDepth) + 8;
    const CaveGround flat = flatGround(kFlatHeight);
    const CaveGround relief = heightfieldGround(heightfield);
    double siteX = 0.0;
    double siteZ = 0.0;
    TEST_CHECK(findRavine(field, flat, kFlatHeight, siteX, siteZ));
    const DepthStats onFlat = scanDepths(field, flat, {0.0, 0.0, 240.0, 240.0, 12.0, 0, bottom});
    const DepthStats onRelief =
        scanDepths(field, relief, {1000.3, -2000.7, 240.0, 240.0, 12.0, 0, bottom});
    const DepthStats ravine =
        scanDepths(field, flat, {siteX - 8.0, siteZ - 8.0, 16.0, 16.0, 1.0, 0, bottom});
    std::printf("roof: %d of %d flat columns, %d of %d relief columns, %d of %d ravine columns "
                "have caves; shallowest air %.1f m (roof %.1f m)\n",
                onFlat.cavedColumns, onFlat.columns, onRelief.cavedColumns, onRelief.columns,
                ravine.cavedColumns, ravine.columns,
                std::min({onFlat.shallowest, onRelief.shallowest, ravine.shallowest}),
                static_cast<double>(params.minRoof));
    const double roof = static_cast<double>(params.minRoof - params.blend / 4.0f);
    const auto check = [roof](const DepthStats& stats, int columns) {
        TEST_CHECK(stats.columns >= columns);
        TEST_CHECK(stats.cavedColumns > 0);
        TEST_CHECK(stats.tooShallow == 0);
        TEST_CHECK(stats.tooDeep == 0);
        TEST_CHECK(stats.shallowest >= roof);
    };
    check(onFlat, 100);
    check(onRelief, 100);
    check(ravine, 50);
}

struct SeaScan {
    int air = 0;
    bool untouched = true;
};

// Carved air over a flat sea floor at the given height, and whether any density changed.
SeaScan scanSea(const CaveField& field, float height) {
    SeaScan scan;
    const int bottom = static_cast<int>(field.params().maxDepth) + 8;
    scanColumns(field, flatGround(height), {0.0, 0.0, 128.0, 128.0, 4.0, -10, bottom},
                [&scan](CaveSampler&, const CaveColumn&, double, float terrain, float carved) {
                    scan.air += carvedAir(terrain, carved) ? 1 : 0;
                    scan.untouched = scan.untouched && carved == terrain;
                });
    return scan;
}

// Under a deep ocean there are no caves; in a shallow sea there are.
void testDeepOcean() {
    const CaveField field(kSeed);
    const float limit = field.params().deepOceanDepth;
    int shallowAir = 0;
    for (const float height : {-100.0f, -(limit - 10.0f)}) {
        const SeaScan scan = scanSea(field, height);
        shallowAir += scan.air;
        TEST_CHECK(scan.air > 0);
    }
    for (const float height : {-300.0f, -(limit + 10.0f)}) {
        TEST_CHECK(scanSea(field, height).untouched);
    }
    // Just past the limit nothing opens, though the density may already feel the void.
    TEST_CHECK(scanSea(field, -(limit + 1.0f)).air == 0);
    std::printf("deep ocean: %d carved air samples in the shallow seas\n", shallowAir);
}

// Rivers keep the caves away: none within riverClearance - 1, the density untouched within
// riverClearance - guard - fade, and caves farther than three clearances.
void testRiversKeepCavesAway() {
    const CaveField field(kSeed);
    const CaveParams& params = field.params();
    const CaveDetail detail;
    const double riverX = 0.0;
    const double clearance = static_cast<double>(params.riverClearance);
    const double untouched = clearance - static_cast<double>(detail.guard + params.fade);
    const int bottom = static_cast<int>(params.maxDepth) + 8;
    int closeSamples = 0;
    int exactSamples = 0;
    int far = 0;
    bool noAir = true;
    bool exact = true;
    scanColumns(field, withStraightRiver(flatGround(kFlatHeight), riverX),
                {-30.0, 0.0, 180.0, 30.0, 3.0, -5, bottom},
                [&](CaveSampler&, const CaveColumn& column, double, float terrain, float carved) {
                    const double distance = std::abs(column.x - riverX);
                    closeSamples += distance <= clearance - 1.0 ? 1 : 0;
                    exactSamples += distance <= untouched ? 1 : 0;
                    far += distance > 3.0 * clearance && carvedAir(terrain, carved) ? 1 : 0;
                    noAir = noAir && (distance > clearance - 1.0 || !carvedAir(terrain, carved));
                    exact = exact && (distance > untouched || carved == terrain);
                });
    std::printf("rivers: %d samples near the river, %d untouched, %d carved air samples "
                "beyond three clearances\n",
                closeSamples, exactSamples, far);
    TEST_CHECK(noAir);
    TEST_CHECK(exact);
    TEST_CHECK(closeSamples > 1000);
    TEST_CHECK(far > 0);
}

// A step of 1 mm along x, y or z changes the carved density by at most kMaxSlope * step, at
// random points near the carved surfaces and around the entrances.
void checkContinuity(const CaveField& field, const CaveGround& ground, std::uint64_t stream,
                     const char* name) {
    constexpr double kStep = 1.0e-3;
    constexpr double kMaxSlope = 100.0;
    constexpr double kSlack = 2.0e-3;
    Rng rng(stream);
    const Area area = areaAroundEntrance(field, ground, 1000.0, -2000.0, 256.0);
    std::vector<Point> candidates = randomPoints(ground, rng, 6000, area, 10.0, 96.0);
    const std::vector<CaveEntrance> entrances = field.entrancesNear(
        area.x - area.half, area.z - area.half, area.x + area.half, area.z + area.half, ground);
    for (const CaveEntrance& e : entrances) {
        const double spread = 1.5 * static_cast<double>(std::max(e.tubeRadius, e.chamberRadius));
        for (int k = 0; k < 400; ++k) {
            candidates.push_back(pointNearEntrance(ground, rng, e, spread));
        }
    }
    CaveSampler sampler(field, ground, CaveDetail{}, boxOf(candidates));
    double worst = 0.0;
    int points = 0;
    bool continuous = true;
    for (const Point& p : candidates) {
        const float here = carvedAt(sampler, p);
        if (std::abs(here) >= 3.0f || here == terrainDensity(p.y, p.ground)) {
            continue;
        }
        ++points;
        const Point shifted[3] = {{p.x + kStep, p.y, p.z, ground.height(p.x + kStep, p.z)},
                                  {p.x, p.y + kStep, p.z, p.ground},
                                  {p.x, p.y, p.z + kStep, ground.height(p.x, p.z + kStep)}};
        for (const Point& q : shifted) {
            const double change = std::abs(static_cast<double>(carvedAt(sampler, q)) -
                                           static_cast<double>(here));
            worst = std::max(worst, change / kStep);
            continuous = continuous && change <= kMaxSlope * kStep + kSlack;
        }
    }
    std::printf("continuity (%s): largest |change| / step = %.2f over %d points, %zu entrances "
                "(limit %.0f)\n",
                name, worst, points, entrances.size(), kMaxSlope);
    TEST_CHECK(continuous);
    TEST_CHECK(points > 100 && !entrances.empty());
}

// No jump of the carved density near the carved surfaces.
void testContinuity() {
    const CaveField field(kSeed);
    const Heightfield heightfield(kSeed);
    checkContinuity(field, flatGround(kFlatHeight), 5, "flat");
    checkContinuity(field, heightfieldGround(heightfield), 6, "relief");
}

// Each LOD carves a subset of the components of the finer one, none at the far terrain, and
// the guard and fade of every LOD that carves stay inside the reach margin of the entrances
// (the far terrain carves nothing, so its guard is not used).
void testLodDetails() {
    const CaveParams params;
    TEST_CHECK(terrain::caveDetailForLod(0).components == terrain::kAllCaveComponents);
    for (int lod = 0; lod < 3; ++lod) {
        const unsigned finer = terrain::caveDetailForLod(lod).components;
        const unsigned coarser = terrain::caveDetailForLod(lod + 1).components;
        TEST_CHECK((coarser & ~finer) == 0U);
    }
    TEST_CHECK(terrain::caveDetailForLod(3).components == 0U);
    for (int lod = 0; lod <= 3; ++lod) {
        const CaveDetail detail = terrain::caveDetailForLod(lod);
        TEST_CHECK(detail.components == 0U ||
                   static_cast<double>(detail.guard + params.fade) < terrain::kEntranceReachMargin);
    }

    // A detail with no component leaves the density alone, where the full detail carves.
    const Heightfield heightfield(kSeed);
    const CaveGround ground = heightfieldGround(heightfield);
    Rng rng(7);
    const std::vector<Point> points =
        randomPoints(ground, rng, 1500, {1000.0, -2000.0, 128.0}, 0.0, 100.0);
    const CaveField field(kSeed);
    const CaveDetail none{0, 4.0f};
    CaveSampler empty(field, ground, none, boxOf(points));
    CaveSampler full(field, ground, CaveDetail{}, boxOf(points));
    bool unchanged = true;
    int carved = 0;
    for (const Point& p : points) {
        const float terrain = terrainDensity(p.y, p.ground);
        unchanged = unchanged && carvedAt(empty, p) == terrain &&
                    caveDensityAt(field, ground, none, p.x, p.y, p.z) == terrain;
        carved += carvedAt(full, p) != terrain ? 1 : 0;
    }
    std::printf("lod details: the full detail touches %d of %zu points, none touches any\n",
                carved, points.size());
    TEST_CHECK(unchanged);
    TEST_CHECK(carved >= 50);
}

// carve(): exactly the terrain from guard + fade of void on, never below it, air inside the
// void, and continuous in the void.
void testCarve() {
    const CaveField field(kSeed);
    const CaveParams& params = field.params();
    const float terrains[] = {-80.0f, -30.0f, -6.0f, -1.5f, -0.2f, 0.0f, 0.3f, 2.0f, 15.0f};
    bool exact = true;
    bool neverBelow = true;
    bool air = true;
    bool continuous = true;
    for (const CaveDetail& detail :
         {CaveDetail{}, terrain::caveDetailForLod(1), terrain::caveDetailForLod(2)}) {
        const float end = detail.guard + params.fade;
        for (const float terrain : terrains) {
            // The fade has a slope of at most 1.5 / fade over a gap of at most |terrain| + end.
            const double slope = 1.0 + 2.0 / static_cast<double>(params.fade) *
                                           (static_cast<double>(std::abs(terrain) + end));
            double previousVoid = -static_cast<double>(params.voidClamp);
            float previous = field.carve(terrain, static_cast<float>(previousVoid), detail);
            for (int i = 1; i <= 6400; ++i) {
                const auto caveVoid = static_cast<float>(-32.0 + 0.01 * i);
                const float carved = field.carve(terrain, caveVoid, detail);
                exact = exact && (caveVoid < end || carved == terrain);
                neverBelow = neverBelow && carved >= terrain;
                air = air && (caveVoid > 0.0f || carved >= 0.0f);
                const double step = static_cast<double>(caveVoid) - previousVoid;
                continuous = continuous && std::abs(static_cast<double>(carved) -
                                                    static_cast<double>(previous)) <=
                                               slope * step + 1.0e-4;
                previousVoid = static_cast<double>(caveVoid);
                previous = carved;
            }
            exact = exact && field.carve(terrain, end, detail) == terrain &&
                    field.carve(terrain, params.voidClamp, detail) == terrain;
        }
    }
    TEST_CHECK(exact);
    TEST_CHECK(neverBelow);
    TEST_CHECK(air);
    TEST_CHECK(continuous);
}

} // namespace

int main() {
    testDeterminism();
    testEveryComponentExists();
    testNothingAboveSurface();
    testRoofThickness();
    testDeepOcean();
    testRiversKeepCavesAway();
    testContinuity();
    testLodDetails();
    testCarve();
    return test::result();
}

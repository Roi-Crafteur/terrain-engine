// Caves under a region with rivers: the ground and the distance to the rivers read from the
// eroded region, no cave near a river, entrances away from the rivers, caves elsewhere, and
// the density of the region from a point and from a sampler. The caves themselves:
// caves_test.cpp; the entrances: cave_entrances_test.cpp. Every parameter is read from the
// field, nothing is hard-coded.
#include "terrain/generation/cave_region.hpp"

#include "terrain/generation/cave_entrances.hpp"
#include "terrain/generation/cliffs.hpp"

#include "caves_test_support.hpp"
#include "legacy_relief_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using caves_test::Area;
using caves_test::BoxBuilder;
using caves_test::carvedAir;
using caves_test::carvedAt;
using caves_test::kSeed;
using caves_test::Point;
using caves_test::Rng;
using caves_test::scanColumns;
using caves_test::terrainDensity;
using caves_test::Window;
using terrain::CaveBox;
using terrain::CaveColumn;
using terrain::CaveEntrance;
using terrain::CaveField;
using terrain::CaveGround;
using terrain::CaveParams;
using terrain::CaveRegion;
using terrain::CaveSampler;
using terrain::HeightmapRegion;

struct Center {
    double x;
    double z;
};

// Centers of the river samples of the region.
std::vector<Center> riverCenters(const CaveRegion& region) {
    const terrain::RiverNetwork& rivers = region.eroded().rivers();
    std::vector<Center> centers;
    for (int i = 0; i < static_cast<int>(rivers.river.size()); ++i) {
        if (rivers.isRiver(i)) {
            centers.push_back({region.eroded().sampleX(i % rivers.size),
                               region.eroded().sampleZ(i / rivers.size)});
        }
    }
    return centers;
}

// Square of the points of the region, scale times as large as it, around its center.
Area areaOf(const CaveRegion& region, double scale) {
    const HeightmapRegion& r = region.eroded().region();
    return {r.centerX, r.centerZ, scale * r.size * r.metersPerPixel / 2.0};
}

// Entrances of the caves of the region over the whole of it, over the given ground.
std::vector<CaveEntrance> entrancesOf(const CaveRegion& region, const CaveGround& ground) {
    const Area a = areaOf(region, 1.0);
    return region.field().entrancesNear(a.x - a.half, a.z - a.half, a.x + a.half, a.z + a.half,
                                        ground);
}

// ---------------------------------------------------------------------------

// The ground is the eroded relief, inside the region and beyond it, and the one of its surface.
void testGround(const CaveRegion& region) {
    Rng rng(1);
    const Area area = areaOf(region, 1.5);
    bool same = true;
    for (int k = 0; k < 500; ++k) {
        const double x = area.x + rng.range(-area.half, area.half);
        const double z = area.z + rng.range(-area.half, area.half);
        same = same && region.groundAt(x, z) == region.eroded().heightAt(x, z) &&
               region.ground().height(x, z) == region.groundAt(x, z);
    }
    bool onSurface = true;
    const terrain::TerrainSurface& surface = region.surface();
    for (int row = 0; row < surface.size(); row += 8) {
        for (int column = 0; column < surface.size(); column += 8) {
            const float ground = region.groundAt(surface.sampleX(column), surface.sampleZ(row));
            onSurface = onSurface && std::abs(ground - surface.at(column, row).ground) < 0.05f;
        }
    }
    TEST_CHECK(same);
    TEST_CHECK(onSurface);
}

// The distance to the rivers: 0 at the river samples, the one of the surface at every sample
// center, and clamped to the border of the region beyond it.
void testRiverDistance(const CaveRegion& region) {
    const std::vector<Center> centers = riverCenters(region);
    bool onRiver = true;
    for (const Center& c : centers) {
        onRiver = onRiver && std::abs(region.riverDistanceAt(c.x, c.z)) < 1.0e-3f;
    }
    const terrain::TerrainSurface& surface = region.surface();
    bool sameAsSurface = true;
    int apart = 0;
    for (int row = 0; row < surface.size(); ++row) {
        for (int column = 0; column < surface.size(); ++column) {
            const float expected = surface.at(column, row).riverDistance;
            const float distance =
                region.riverDistanceAt(surface.sampleX(column), surface.sampleZ(row));
            sameAsSurface = sameAsSurface &&
                            std::abs(distance - expected) <= 1.0e-3f + 1.0e-6f * expected;
            apart += expected > 2.0f * region.field().params().riverClearance ? 1 : 0;
        }
    }
    const double first = surface.sampleX(0);
    const double last = surface.sampleX(surface.size() - 1);
    const double middle = surface.sampleZ(surface.size() / 2);
    const bool clamped = region.riverDistanceAt(first - 3000.0, middle) ==
                             region.riverDistanceAt(first, middle) &&
                         region.riverDistanceAt(last + 3000.0, middle) ==
                             region.riverDistanceAt(last, middle);
    std::printf("rivers: %zu river samples of %d, %d samples farther than two clearances\n",
                centers.size(), surface.size() * surface.size(), apart);
    TEST_CHECK(centers.size() >= 20);
    TEST_CHECK(onRiver);
    TEST_CHECK(sameAsSurface);
    TEST_CHECK(clamped);
    TEST_CHECK(apart > 1000);
}

// Above the ground the density is y - ground exactly, away from the entrances; below it, the
// caves only open the rock.
void testDensityAroundGround(const CaveRegion& region) {
    Rng rng(2);
    const Area area = areaOf(region, 1.2);
    bool exact = true;
    bool neverAdds = true;
    int touched = 0;
    for (int k = 0; k < 400; ++k) {
        const double x = area.x + rng.range(-area.half, area.half);
        const double z = area.z + rng.range(-area.half, area.half);
        const float ground = region.groundAt(x, z);
        const std::vector<CaveEntrance> entrances =
            region.field().entrancesNear(x, z, x, z, region.ground());
        const bool atEntrance =
            CaveField::nearEntrance(x, z, entrances, terrain::kEntranceReachMargin);
        const double above = static_cast<double>(ground) + rng.range(0.0, 30.0);
        const double below = static_cast<double>(ground) - rng.range(0.0, 110.0);
        const float carved = region.densityAt(x, below, z);
        exact = exact &&
                (atEntrance || region.densityAt(x, above, z) == terrainDensity(above, ground));
        neverAdds = neverAdds && carved >= terrainDensity(below, ground);
        touched += carved != terrainDensity(below, ground) ? 1 : 0;
    }
    std::printf("density: %d of 400 points under the ground touched by the caves\n", touched);
    TEST_CHECK(exact);
    TEST_CHECK(neverAdds);
    TEST_CHECK(touched > 20);
}

// A sampler of a box of the region gives the density of its points, the one of densityAt.
void testSamplerMatchesPoints(const CaveRegion& region) {
    Rng rng(5);
    const Area area = areaOf(region, 0.2);
    std::vector<Point> points;
    for (int k = 0; k < 150; ++k) {
        const double x = area.x + rng.range(-area.half, area.half);
        const double z = area.z + rng.range(-area.half, area.half);
        const float ground = region.groundAt(x, z);
        points.push_back({x, static_cast<double>(ground) - rng.range(0.0, 100.0), z, ground});
    }
    CaveSampler sampler = region.sampler(caves_test::boxOf(points));
    bool same = true;
    for (const Point& p : points) {
        same = same && carvedAt(sampler, p) == region.densityAt(p.x, p.y, p.z);
    }
    TEST_CHECK(same);
}

// Around some river samples: no carved air where the river is closer than the clearance minus
// 1 m, and the density untouched where it is closer than clearance - guard - fade.
void testNoCavesNearRivers(const CaveRegion& region) {
    const CaveParams& params = region.field().params();
    const terrain::CaveDetail detail;
    const double clearance = static_cast<double>(params.riverClearance);
    const double untouched = clearance - static_cast<double>(detail.guard + params.fade);
    const std::vector<Center> centers = riverCenters(region);
    const std::size_t windows = 6;
    const int bottom = static_cast<int>(params.maxDepth) + 4;
    int close = 0;
    int airAway = 0;
    bool noAir = true;
    bool exact = true;
    const auto visit = [&](CaveSampler&, const CaveColumn& column, double, float terrain,
                           float carved) {
        const double distance = static_cast<double>(region.riverDistanceAt(column.x, column.z));
        close += distance < clearance - 1.0 ? 1 : 0;
        airAway += distance > clearance && carvedAir(terrain, carved) ? 1 : 0;
        noAir = noAir && (distance >= clearance - 1.0 || !carvedAir(terrain, carved));
        exact = exact && (distance > untouched || carved == terrain);
    };
    for (std::size_t n = 0; n < windows && n < centers.size(); ++n) {
        const Center& c = centers[n * centers.size() / windows];
        scanColumns(region.field(), region.ground(),
                    Window{c.x - 40.0, c.z - 40.0, 80.0, 80.0, 4.0, 0, bottom}, visit);
    }
    std::printf("near rivers: %d samples closer than the clearance, %d carved air samples "
                "farther\n",
                close, airAway);
    TEST_CHECK(close > 1000);
    TEST_CHECK(airAway > 0);
    TEST_CHECK(noAir);
    TEST_CHECK(exact);
}

// Caves exist in the region, away from the rivers.
void testCavesAwayFromRivers(const CaveRegion& region) {
    Rng rng(3);
    const Area area = areaOf(region, 0.7);
    const CaveParams& params = region.field().params();
    int air = 0;
    const auto visit = [&](CaveSampler&, const CaveColumn& column, double, float terrain,
                           float carved) {
        const float river = region.riverDistanceAt(column.x, column.z);
        air += river > 2.0f * params.riverClearance && carvedAir(terrain, carved) ? 1 : 0;
    };
    for (int n = 0; n < 4; ++n) {
        const double x = area.x + rng.range(-area.half, area.half);
        const double z = area.z + rng.range(-area.half, area.half);
        scanColumns(region.field(), region.ground(),
                    Window{x, z, 64.0, 64.0, 4.0, static_cast<int>(params.minRoof),
                           static_cast<int>(params.maxDepth)},
                    visit);
    }
    std::printf("away from rivers: %d carved air samples in four windows\n", air);
    TEST_CHECK(air > 100);
}

// Distance to the nearest river along the axis of an entrance, by the region.
float nearestRiver(const CaveRegion& region, const CaveEntrance& e) {
    float nearest = terrain::kNoRiverDistance;
    for (int i = 0; i <= 20; ++i) {
        const double t = i / 20.0;
        nearest = std::min(nearest, region.riverDistanceAt(e.topX + t * (e.bottomX - e.topX),
                                                           e.topZ + t * (e.bottomZ - e.topZ)));
    }
    return nearest;
}

float largestRadius(const CaveEntrance& e) {
    return std::max(e.tubeRadius, e.chamberRadius);
}

// The entrances of the region are those of the same ground without rivers that keep clear of
// them. The entrances dropped by the rivers are returned.
std::vector<CaveEntrance> testEntrancesAwayFromRivers(const CaveRegion& region) {
    const CaveParams& params = region.field().params();
    CaveGround dry = region.ground();
    dry.riverDistance = nullptr;
    const std::vector<CaveEntrance> kept = entrancesOf(region, region.ground());
    const std::vector<CaveEntrance> all = entrancesOf(region, dry);
    bool apart = true;
    for (const CaveEntrance& e : kept) {
        apart = apart && region.riverDistanceAt(e.topX, e.topZ) >= params.riverClearance &&
                nearestRiver(region, e) >= params.riverClearance + largestRadius(e);
    }
    std::size_t next = 0;
    bool identical = true;
    bool droppedNear = true;
    std::vector<CaveEntrance> dropped;
    for (const CaveEntrance& e : all) {
        if (next < kept.size() && kept[next].cellX == e.cellX && kept[next].cellZ == e.cellZ) {
            identical = identical && kept[next] == e;
            ++next;
        } else {
            dropped.push_back(e);
            droppedNear = droppedNear && nearestRiver(region, e) <
                                             params.riverClearance + largestRadius(e) + params.fade;
        }
    }
    std::printf("entrances: %zu in the region, %zu more without its rivers\n", kept.size(),
                dropped.size());
    TEST_CHECK(kept.size() >= 5);
    TEST_CHECK(apart);
    TEST_CHECK(next == kept.size() && identical && droppedNear);
    return dropped;
}

// A sampler holds the entrances of its box, over the ground with the rivers of the region: the
// entrance kept by the rivers, not the one they dropped.
void testSamplerEntrances(const CaveRegion& region, const std::vector<CaveEntrance>& dropped) {
    const std::vector<CaveEntrance> kept = entrancesOf(region, region.ground());
    TEST_CHECK(!kept.empty() && !dropped.empty());
    if (kept.empty() || dropped.empty()) {
        return;
    }
    for (const CaveEntrance& e : {kept.front(), dropped.front()}) {
        const double reach = region.field().entranceReach(e);
        const CaveBox box{e.topX - reach, e.bottomY - reach, e.topZ - reach,
                          e.topX + reach, e.topY + reach, e.topZ + reach};
        const CaveSampler sampler = region.sampler(box);
        const std::vector<CaveEntrance>& held = sampler.entrances();
        const bool listed = std::find(held.begin(), held.end(), e) != held.end();
        const bool isKept = std::find(kept.begin(), kept.end(), e) != kept.end();
        TEST_CHECK(listed == isKept);
        TEST_CHECK(held == region.field().entrancesNear(box.minX, box.minZ, box.maxX, box.maxZ,
                                                        region.ground()));
    }
}

} // namespace

int main() {
    const HeightmapRegion site =
        terrain::findCliffRegion(kSeed, 50.0, 129, {}, test_relief::legacyRelief());
    const CaveRegion region(kSeed, site, test_relief::onLegacyRelief(terrain::CaveRegionParams{}));
    testGround(region);
    testRiverDistance(region);
    testDensityAroundGround(region);
    testSamplerMatchesPoints(region);
    testNoCavesNearRivers(region);
    testCavesAwayFromRivers(region);
    testSamplerEntrances(region, testEntrancesAwayFromRivers(region));
    return test::result();
}

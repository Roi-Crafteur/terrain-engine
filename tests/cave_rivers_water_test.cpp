// Water of the underground rivers (Phase 8C-2): the surface of a river never slopes more than
// CaveRiverParams::maxSlope, so that no water stands as a wall against dry air (the step between
// two samples 1 m apart stays within that slope) and none lies over the ground; along the
// galleries of the Rivers regions the water is continuous but where a barrier dams it, a sill
// ends it, the limits of the caves cut it or the level gets too steep, all counted apart; the
// water of a gallery is connected in 3D; LOD 0 and LOD 1 hold the same water, LOD 2 none.
#include "terrain/generation/cave_region.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/caves.hpp"
#include "terrain/generation/cliffs.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/cave_tiles.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "aquifers_test_support.hpp"
#include "cave_districts_carving_support.hpp"
#include "cave_galleries_test_support.hpp"
#include "caves_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <queue>
#include <vector>

namespace {

using caves_test::kSeed;
using caves_test::Rng;
using terrain::AquiferCache;
using terrain::CaveColumn;
using terrain::CaveDistrictType;
using terrain::CaveField;
using terrain::CaveGround;
using terrain::CaveParams;
using terrain::CaveProfile;
using terrain::CaveSampler;

// The floor of the galleries lies 0.6 m over a sample of the 1 m lattices: their river,
// 0.9 m deep, holds a sample.
constexpr float kFlatHeight = 300.4f;

// Depth of the water of a river column: maxDepth times the flatness of its level.
double riverDepth(const CaveField& field, const CaveColumn& column) {
    return static_cast<double>(column.riverLevel) - static_cast<double>(column.galleryFloor) +
           static_cast<double>(field.params().rivers.minDepth);
}

// Points within 0.3 m of the axis of a gallery whose river is at least minDepth deep, searched
// on a grid over size x size meters from (x0, z0), at most one per 60 m row band.
std::vector<std::array<double, 2>> riverPoints(const CaveField& field, const CaveGround& ground,
                                               double x0, double z0, double size, int count,
                                               double minDepth) {
    std::vector<std::array<double, 2>> points;
    AquiferCache cache(field, ground);
    for (double z = z0; z < z0 + size && static_cast<int>(points.size()) < count; z += 3.0) {
        for (double x = x0; x < x0 + size; x += 0.5) {
            const CaveColumn c = field.column(x, z, ground.height(x, z), ground, {}, &cache, false);
            if (c.galleryDistance < 0.3f && c.riverLevel > terrain::kDryLevel &&
                riverDepth(field, c) >= minDepth) {
                points.push_back({x, z});
                z += 60.0;
                break;
            }
        }
    }
    return points;
}

// The flatness thresholds derived from maxSlope keep the surface of a river within it.
void testParameters() {
    const CaveParams params;
    const terrain::CaveRiverFlatness flat = terrain::riverFlatness(params.rivers, params.galleries);
    const double bound = terrain::riverSlopeBound(params.rivers, params.galleries);
    std::printf("parameters: flatness %.4f to %.4f, surface slope at most %.4f\n", flat.low,
                flat.high, bound);
    TEST_CHECK(flat.low > 0.0 && flat.high > flat.low);
    TEST_CHECK(bound <= static_cast<double>(params.rivers.maxSlope) + 1e-12);
}

// Lattices of 1 m around river points of a world of Rivers regions, on flat ground and on the
// relief of the heightfield: water only in cave air under the ground and under its level, no
// dry cave air under it, no wall (the steps of a sloped surface within maxSlope).
void testNoWalls() {
    const CaveField field(kSeed, galleries_test::uniformParams(CaveDistrictType::Rivers));
    const terrain::Heightfield heightfield(kSeed);
    const CaveGround flat = caves_test::flatGround(kFlatHeight);
    const CaveGround relief = caves_test::heightfieldGround(heightfield);
    aquifers_test::LakeCounts total;
    int scans = 0;
    for (const CaveGround* ground : {&flat, &relief}) {
        for (const auto& p : riverPoints(field, *ground, -1000.0, -2000.0, 1500.0, 4, 0.5)) {
            AquiferCache cache(field, *ground);
            const CaveColumn c =
                field.column(p[0], p[1], ground->height(p[0], p[1]), *ground, {}, &cache, false);
            const caves_test::Lattice lattice{static_cast<int>(p[0]) - 24,
                                              static_cast<int>(c.galleryFloor) - 4,
                                              static_cast<int>(p[1]) - 24,
                                              49,
                                              10,
                                              49};
            const aquifers_test::Scan scan(field, *ground, terrain::CaveDetail{}, lattice);
            total.add(aquifers_test::countLakes(field, scan, scan.carved(), scan.water()));
            ++scans;
        }
    }
    std::printf("rivers: %d lattices, %zu water samples (%zu in the skin of the rock), %zu over "
                "their level, %zu over the ground, %zu of dry air under the water, %zu walls, "
                "%zu steps of a sloped surface\n",
                scans, total.water, total.inRock, total.aboveLevel, total.aboveGround,
                total.dryAir, total.walls, total.steps);
    TEST_CHECK(scans == 8);
    TEST_CHECK(total.water > 1500);
    TEST_CHECK(total.outsideAir == 0 && total.aboveLevel == 0 && total.aboveGround == 0);
    TEST_CHECK(total.dryAir == 0 && total.walls == 0 && total.dryColumns == 0);
}

// On the relief, between columns 1 m apart where both rivers hold water, the level changes by
// maxSlope at most.
void testSlope() {
    const CaveField field(kSeed, galleries_test::uniformParams(CaveDistrictType::Rivers));
    const terrain::Heightfield heightfield(kSeed);
    const CaveGround ground = caves_test::heightfieldGround(heightfield);
    AquiferCache cache(field, ground);
    const double minDepth = static_cast<double>(field.params().rivers.minDepth);
    Rng rng(17);
    int pairs = 0;
    double steepest = 0.0;
    for (int n = 0; n < 40000; ++n) {
        const double x = rng.range(-4000.0, 4000.0);
        const double z = rng.range(-4000.0, 4000.0);
        const double angle = rng.range(0.0, 6.283185307179586);
        const double x2 = x + std::cos(angle);
        const double z2 = z + std::sin(angle);
        const CaveColumn a = field.column(x, z, ground.height(x, z), ground, {}, &cache, false);
        const CaveColumn b = field.column(x2, z2, ground.height(x2, z2), ground, {}, &cache,
                                          false);
        if (riverDepth(field, a) <= minDepth || riverDepth(field, b) <= minDepth) {
            continue;
        }
        ++pairs;
        steepest = std::max(steepest, std::abs(static_cast<double>(a.riverLevel) -
                                               static_cast<double>(b.riverLevel)));
    }
    std::printf("slope: %d pairs 1 m apart with water, steepest step %.4f m\n", pairs, steepest);
    TEST_CHECK(pairs > 5000);
    TEST_CHECK(steepest <= static_cast<double>(field.params().rivers.maxSlope) + 1e-4);
}

// What lies just under the surface of a river on the axis of its gallery.
struct AxisCounts {
    int water = 0;
    int dry = 0;   // expected water, none: a failure
    int steep = 0; // too steep for water
    int sill = 0;  // within the sill of the threshold
    int dam = 0;   // rock of a barrier
    int cut = 0;   // the limits of the caves (roof, deepest caves, rivers above, deep sea)

    [[nodiscard]] double share() const {
        return water + dry == 0 ? 0.0 : static_cast<double>(water) / (water + dry);
    }
};

// Axis samples of the window (step meters apart, within 1 m of an axis, past the sill), each
// read from a sampler of its 100 m tile.
void countAxis(const CaveField& field, const CaveGround& ground, double x0, double z0,
               double size, AxisCounts& counts) {
    const terrain::CaveRiverParams& r = field.params().rivers;
    for (double tz = z0; tz < z0 + size; tz += 100.0) {
        for (double tx = x0; tx < x0 + size; tx += 100.0) {
            CaveSampler sampler(field, ground, terrain::CaveDetail{},
                                {tx, -600.0, tz, tx + 100.0, 2400.0, tz + 100.0});
            for (double z = tz; z < tz + 100.0; z += 2.0) {
                for (double x = tx; x < tx + 100.0; x += 2.0) {
                    const CaveColumn c = sampler.column(x, z, ground.height(x, z));
                    const float river = c.profile[CaveProfile::kRiver];
                    if (c.galleryDistance >= 1.0f || river < r.threshold) {
                        continue;
                    }
                    if (river < r.threshold + r.sillWidth) {
                        ++counts.sill;
                        continue;
                    }
                    if (riverDepth(field, c) < static_cast<double>(r.minDepth) + 0.1) {
                        ++counts.steep;
                        continue;
                    }
                    const double y = static_cast<double>(c.riverLevel) - 0.04;
                    const float rock = caves_test::terrainDensity(y, c.ground);
                    const float carved = sampler.density(c, y, rock);
                    if (field.limit(c, y) >= 0.0f) {
                        ++counts.cut;
                    } else if (sampler.voids(c, y).barrier > 0.0f) {
                        ++counts.dam;
                    } else {
                        const bool wet = sampler.waterDensity(c, y, rock, carved) < 0.0f;
                        (wet ? counts.water : counts.dry) += 1;
                    }
                }
            }
        }
    }
}

// Along the galleries of the Rivers regions on land of the streamed world, over 400 m squares
// around their sites, until 3 of them hold axis samples expected to hold water (where the
// relief is flat enough), 400 at least: 97 % of them do. Returns a site with water.
std::array<double, 2> testContinuity() {
    CaveParams params;
    params.districts = terrain::CaveDistrictParams{};
    const terrain::TerrainTiles tiles(kSeed, {}, {}, params);
    const CaveField& field = *tiles.caves();
    const CaveGround& ground = tiles.caveGround();
    const std::vector<terrain::CaveDistrict> rivers = districts_test::districtsOf(
        field, ground, {-12, 25}, CaveDistrictType::Rivers, 5.0f);
    AxisCounts counts;
    std::array<double, 2> wetSite{0.0, 0.0};
    int regions = 0;
    int wetRegions = 0;
    for (const terrain::CaveDistrict& d : rivers) {
        if (wetRegions >= 3 && counts.water + counts.dry >= 400) {
            break;
        }
        const int before = counts.water + counts.dry;
        countAxis(field, ground, d.siteX - 200.0, d.siteZ - 200.0, 400.0, counts);
        ++regions;
        if (counts.water + counts.dry > before) {
            wetSite = wetRegions == 0 ? std::array<double, 2>{d.siteX, d.siteZ} : wetSite;
            ++wetRegions;
        }
    }
    std::printf("continuity: %d Rivers regions (%d with water), %d axis samples with water, %d "
                "without; apart: %d too steep, %d in sills, %d dammed, %d cut by the limits "
                "(%.1f%%)\n",
                regions, wetRegions, counts.water, counts.dry, counts.steep, counts.sill,
                counts.dam, counts.cut, 100.0 * counts.share());
    TEST_CHECK(wetRegions >= 3);
    TEST_CHECK(counts.water + counts.dry >= 400);
    TEST_CHECK(counts.share() >= 0.97);
    return wetSite;
}

// Under the eroded relief of a CaveRegion around a site where the streamed world holds rivers,
// whose surface rivers keep the caves riverClearance away: the galleries of Rivers regions are
// cut under the surface rivers, counted apart, and the water is continuous elsewhere.
void testRegionWithRivers(const std::array<double, 2>& wetSite) {
    terrain::CaveRegionParams params;
    params.caves = galleries_test::uniformParams(CaveDistrictType::Rivers);
    const terrain::HeightmapRegion site{wetSite[0], wetSite[1], 20.0, 97};
    const terrain::CaveRegion region(kSeed, site, params);
    AxisCounts counts;
    countAxis(region.field(), region.ground(), site.centerX - 400.0, site.centerZ - 400.0,
              800.0, counts);
    std::printf("eroded region: %d axis samples with water, %d without; apart: %d too steep, "
                "%d dammed, %d cut by the limits and the surface rivers (%.1f%%)\n",
                counts.water, counts.dry, counts.steep, counts.dam, counts.cut,
                100.0 * counts.share());
    TEST_CHECK(counts.water > 100);
    TEST_CHECK(counts.share() >= 0.97);

    // A straight surface river across a world of Rivers regions on flat ground: the galleries
    // under it are cut by riverClearance, counted apart, never left dry.
    const CaveField field(kSeed, galleries_test::uniformParams(CaveDistrictType::Rivers));
    const CaveGround crossed =
        caves_test::withStraightRiver(caves_test::flatGround(kFlatHeight), 1300.0);
    AxisCounts straight;
    countAxis(field, crossed, 1000.0, -2000.0, 600.0, straight);
    std::printf("straight river: %d axis samples with water, %d without, %d cut by it\n",
                straight.water, straight.dry, straight.cut);
    TEST_CHECK(straight.cut > 0 && straight.dry == 0 && straight.water > 100);
}

// From a river point of a world of Rivers regions on flat ground, the water of the gallery is
// one 6-connected body reaching out of a 120 m square on two sides at least; LOD 0 and LOD 1
// hold water at the same samples of the band, LOD 2 none.
void testConnectedAndLods() {
    const CaveField field(kSeed, galleries_test::uniformParams(CaveDistrictType::Rivers));
    const CaveGround ground = caves_test::flatGround(kFlatHeight);
    const auto points = riverPoints(field, ground, 1000.0, -2000.0, 1500.0, 2, 0.5);
    TEST_CHECK(points.size() == 2);
    int bodies = 0;
    std::size_t compared = 0;
    std::size_t differ = 0;
    std::size_t lod2Water = 0;
    for (const auto& p : points) {
        const double floor = kFlatHeight - field.params().galleries.floorDepth;
        const caves_test::Lattice lattice{static_cast<int>(p[0]) - 60, static_cast<int>(floor) - 1,
                                          static_cast<int>(p[1]) - 60, 121, 4, 121};
        const aquifers_test::Scan lod0(field, ground, terrain::caveDetailForLod(0), lattice);
        const aquifers_test::Scan lod1(field, ground, terrain::caveDetailForLod(1), lattice);
        const aquifers_test::Scan lod2(field, ground, terrain::caveDetailForLod(2), lattice);
        for (std::size_t n = 0; n < lattice.count(); ++n) {
            const bool wet0 = lod0.water()[n] < 0.0f;
            ++compared;
            differ += wet0 != (lod1.water()[n] < 0.0f) ? 1 : 0;
            lod2Water += lod2.water()[n] < 0.0f ? 1 : 0;
        }
        // The body of water holding the sample under the river point.
        std::vector<unsigned char> seen(lattice.count(), 0);
        std::queue<std::array<int, 3>> queue;
        const int start = static_cast<int>(p[0]) - lattice.x0;
        const int startK = static_cast<int>(p[1]) - lattice.z0;
        for (int j = 0; j < lattice.ny; ++j) {
            const std::size_t n = lattice.index(start, j, startK);
            if (lod0.water()[n] < 0.0f && seen[n] == 0) {
                seen[n] = 1;
                queue.push({start, j, startK});
            }
        }
        unsigned sides = 0;
        while (!queue.empty()) {
            const auto [i, j, k] = queue.front();
            queue.pop();
            sides |= (i == 0 ? 1U : 0U) | (i == lattice.nx - 1 ? 2U : 0U) |
                     (k == 0 ? 4U : 0U) | (k == lattice.nz - 1 ? 8U : 0U);
            constexpr std::array<std::array<int, 3>, 6> kSteps{
                {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
            for (const auto& s : kSteps) {
                const int a = i + s[0];
                const int b = j + s[1];
                const int c = k + s[2];
                if (a < 0 || b < 0 || c < 0 || a >= lattice.nx || b >= lattice.ny ||
                    c >= lattice.nz) {
                    continue;
                }
                const std::size_t n = lattice.index(a, b, c);
                if (lod0.water()[n] < 0.0f && seen[n] == 0) {
                    seen[n] = 1;
                    queue.push({a, b, c});
                }
            }
        }
        int count = 0;
        for (unsigned bit = 1; bit <= 8; bit <<= 1) {
            count += (sides & bit) != 0 ? 1 : 0;
        }
        bodies += count >= 2 ? 1 : 0;
    }
    std::printf("connected: %d bodies of water leaving their square on 2 sides; LOD 1 differs "
                "from LOD 0 at %zu of %zu samples, LOD 2 holds %zu\n",
                bodies, differ, compared, lod2Water);
    TEST_CHECK(bodies == 2);
    TEST_CHECK(differ == 0);
    TEST_CHECK(lod2Water == 0);
}

} // namespace

int main() {
    testParameters();
    testNoWalls();
    testSlope();
    const std::array<double, 2> wetSite = testContinuity();
    testRegionWithRivers(wetSite);
    testConnectedAndLods();
    return test::result();
}

// Underground rivers of the caves (Phase 8C-2), their banks and sills: around a river the band
// of rock closes every cave but the gallery, so that the only carved air of the band is the one
// of the gallery; the sill closes the bottom of the gallery where the river field crosses the
// threshold of the water; entrances never cross a band, and nothing of it exists without cave
// regions or with neutral ones.
#include "terrain/generation/aquifers.hpp"
#include "terrain/generation/cave_sampler.hpp"
#include "terrain/generation/caves.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "cave_galleries_test_support.hpp"
#include "cave_environment_test_support.hpp"
#include "caves_test_support.hpp"
#include "legacy_relief_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using caves_test::kSeed;
using caves_test::Rng;
using terrain::AquiferCache;
using terrain::CaveColumn;
using terrain::CaveDistrictType;
using terrain::CaveEntrance;
using terrain::CaveField;
using terrain::CaveParams;
using terrain::CaveProfile;
using terrain::CaveSampler;
using terrain::CaveVoids;

constexpr float kFlatHeight = 300.0f;

// Rock of the band of the river of a column at height y: positive inside the band.
double bandRock(const CaveField& field, const CaveColumn& column, double y) {
    const CaveParams& p = field.params();
    const double halfWidth = galleries_test::halfWidthOf(field, column);
    const double floor = static_cast<double>(column.galleryFloor);
    return std::min({halfWidth + p.rivers.shell - static_cast<double>(column.galleryDistance),
                     y - (floor - p.rivers.shell), floor + p.rivers.maxDepth +
                                                       p.rivers.freeboard - y});
}

// Points on the axes of the galleries of the field, over flat ground, on a grid of step meters.
std::vector<std::array<double, 2>> axisPoints(const CaveField& field,
                                              const terrain::CaveGround& ground, double x0,
                                              double z0, int count) {
    std::vector<std::array<double, 2>> points;
    AquiferCache cache(field, ground);
    for (int k = 0; k < 400 && static_cast<int>(points.size()) < count; ++k) {
        for (int i = 0; i < 400 && static_cast<int>(points.size()) < count; i += 7) {
            const double x = x0 + 4.0 * i;
            const double z = z0 + 9.0 * k;
            const CaveColumn c = field.column(x, z, ground.height(x, z), ground, {}, &cache, false);
            if (c.galleryDistance < 0.3f) {
                points.push_back({x, z});
                k += 20; // spread the points along the network
                break;
            }
        }
    }
    return points;
}

// In a world of Rivers regions on flat ground, around points of the axes: every carved air
// sample of the band is air of the gallery, although the other caves reach into the band.
void testBandIsolation() {
    const CaveField field(kSeed, galleries_test::uniformParams(CaveDistrictType::Rivers));
    const terrain::CaveGround ground = caves_test::flatGround(kFlatHeight);
    const auto points = axisPoints(field, ground, 1000.0, -2000.0, 6);
    TEST_CHECK(points.size() == 6);
    int bandAir = 0;
    int foreign = 0;
    int closed = 0;
    for (const auto& p : points) {
        CaveSampler sampler(field, ground, terrain::CaveDetail{},
                            {p[0] - 20.0, 250.0, p[1] - 20.0, p[0] + 20.0, 290.0, p[1] + 20.0});
        for (int k = -20; k <= 20; ++k) {
            for (int i = -20; i <= 20; ++i) {
                const CaveColumn column = sampler.column(p[0] + i, p[1] + k, kFlatHeight);
                for (double y = 266.0; y <= 282.0; y += 1.0) {
                    if (bandRock(field, column, y) <= 0.0) {
                        continue;
                    }
                    const float t = caves_test::terrainDensity(y, kFlatHeight);
                    const CaveVoids v = sampler.voids(column, y);
                    const float others = std::min({v.spaghetti, v.noodles, v.cheese, v.ravines});
                    closed += others < 0.0f && v.galleries >= 0.0f ? 1 : 0;
                    if (caves_test::carvedAir(t, sampler.density(column, y, t))) {
                        ++bandAir;
                        foreign += v.galleries < 0.0f ? 0 : 1;
                    }
                }
            }
        }
    }
    std::printf("bands: %d samples of carved air, %d not in the gallery; %d samples of other "
                "caves closed\n",
                bandAir, foreign, closed);
    TEST_CHECK(bandAir > 500);
    TEST_CHECK(foreign == 0);
    TEST_CHECK(closed > 100);
}

// The sill: rock across the band where the river field is within sillWidth of the threshold,
// growing continuously from nothing; neither bank nor sill under bankLow.
void testSill() {
    const CaveField field(kSeed, galleries_test::uniformParams(CaveDistrictType::Rivers));
    const terrain::CaveRiverParams& r = field.params().rivers;
    const float clamp = field.params().voidClamp;
    CaveColumn column;
    column.profile[CaveProfile::kGalleries] = 1.0f;
    column.galleryDistance = 0.0f;
    column.galleryFloor = 100.0f;
    const double y = 100.5;
    float previous = -clamp;
    float largestStep = 0.0f;
    bool placed = true;
    for (int n = 0; n <= 1000; ++n) {
        const float field01 = static_cast<float>(n) / 1000.0f;
        column.profile[CaveProfile::kRiver] = field01;
        const float sill = field.sillVoid(column, y);
        const float offset = std::abs(field01 - r.threshold);
        placed = placed && (offset < r.sillWidth * 0.9f ? sill > 0.0f : true) &&
                 (offset > 2.0f * r.sillWidth ? sill == -clamp : true) &&
                 (field01 <= r.bankLow ? field.bankVoid(column, y) == -clamp : true) &&
                 (field01 >= r.bankHigh ? field.bankVoid(column, y) > 0.0f : true);
        largestStep = std::max(largestStep, std::abs(sill - previous));
        previous = sill;
    }
    std::printf("sill: largest step %.3f m per 0.001 of the field\n", largestStep);
    TEST_CHECK(placed);
    TEST_CHECK(largestStep < 1.0f);
}

// Smallest clearance of an entrance over the tops of the bands it passes beside (its axis within
// the band plus slack times its radius horizontally), every 1 % of its axis; +infinity if it
// passes none.
double bandClearance(const CaveField& field, const terrain::CaveGround& ground,
                     AquiferCache& cache, const CaveEntrance& e, double slack) {
    double clearance = 1.0e9;
    for (int s = 0; s <= 100; ++s) {
        const double t = s / 100.0;
        const double x = e.topX + t * (e.bottomX - e.topX);
        const double y = e.topY + t * (e.bottomY - e.topY);
        const double z = e.topZ + t * (e.bottomZ - e.topZ);
        const CaveColumn c = field.column(x, z, ground.height(x, z), ground, {}, &cache, false);
        const double radius = s == 100 ? e.chamberRadius : e.tubeRadius;
        const double top = static_cast<double>(field.riverBandTop(c));
        const double band =
            galleries_test::halfWidthOf(field, c) + field.params().rivers.shell + slack * radius;
        if (top > terrain::kDryLevel && static_cast<double>(c.galleryDistance) < band) {
            clearance = std::min(clearance, y - radius - top);
        }
    }
    return clearance;
}

// The streamed world with the default cave regions, around 6 Rivers regions on land: every
// entrance passing beside a band stays above it, so that the bank never closes an entrance; the
// entrances that the same world without rivers keeps and this one drops all pass within the
// reach of the filter (AquiferCache::riverBandTopNear: the band of the columns a radius away,
// 1.5 radius wider) of a band less than the entrance margin under them, plus the rise of the
// floor over a radius (a slope of 0.5).
void testEntrancesClear() {
    CaveParams params;
    params.districts = terrain::CaveDistrictParams{};
    const terrain::TerrainTiles tiles(kSeed, test_relief::legacyRelief(), {}, params);
    const CaveField& field = *tiles.caves();
    const terrain::CaveGround& ground = tiles.caveGround();
    CaveParams dry = params;
    dry.districts->profiles[static_cast<std::size_t>(CaveDistrictType::Rivers)]
                           [CaveProfile::kRiver] = 0.0f;
    const CaveField withoutRivers(kSeed, dry);
    AquiferCache cache(field, ground);
    AquiferCache dryCache(withoutRivers, ground);
    int regions = 0;
    int beside = 0;
    int crossing = 0;
    int dropped = 0;
    int droppedFar = 0;
    for (int n = 0; n < 225 && regions < 6; ++n) {
        const terrain::CaveDistrict d = field.district(n % 15 - 7, n / 15 - 7, ground);
        if (d.type != CaveDistrictType::Rivers || ground.height(d.siteX, d.siteZ) < 20.0f) {
            continue;
        }
        ++regions;
        const double x0 = d.siteX - 700.0;
        const double z0 = d.siteZ - 700.0;
        const std::vector<CaveEntrance> kept =
            field.entrancesNear(x0, z0, x0 + 1400.0, z0 + 1400.0, ground, cache);
        for (const CaveEntrance& e : kept) {
            const double clearance = bandClearance(field, ground, cache, e, 1.0);
            beside += clearance < 1.0e9 ? 1 : 0;
            crossing += clearance < 0.0 ? 1 : 0;
        }
        for (const CaveEntrance& e :
             withoutRivers.entrancesNear(x0, z0, x0 + 1400.0, z0 + 1400.0, ground, dryCache)) {
            if (std::find(kept.begin(), kept.end(), e) != kept.end()) {
                continue;
            }
            ++dropped;
            const double margin = static_cast<double>(field.params().aquifers.entranceMargin);
            // The filter reads the band top of columns a radius away: the floor may rise there.
            const double rise = 0.5 * static_cast<double>(e.chamberRadius);
            droppedFar += bandClearance(field, ground, cache, e, 3.5) >= margin + rise ? 1 : 0;
        }
    }
    std::printf("entrances: %d Rivers regions, %d beside a band (%d crossing it); %d dropped "
                "for the rivers, %d of them out of the reach of the filter\n",
                regions, beside, crossing, dropped, droppedFar);
    TEST_CHECK(regions == 6);
    TEST_CHECK(crossing == 0);
    TEST_CHECK(droppedFar == 0);
}

// Without cave regions, and with neutral ones, the band is nowhere: the entrance filter reads the
// same levels.
void testInactive() {
    const terrain::Heightfield heightfield(kSeed, test_relief::legacyRelief());
    const terrain::CaveGround ground = caves_test::heightfieldGround(heightfield);
    terrain::CaveDistrictParams neutral;
    neutral.profiles.fill(CaveProfile{});
    CaveParams withNeutral;
    withNeutral.districts = neutral;
    const CaveField without(kSeed);
    const CaveField neutralField(kSeed, withNeutral);
    AquiferCache a(without, ground);
    AquiferCache b(neutralField, ground);
    Rng rng(3);
    bool same = true;
    for (int n = 0; n < 3000; ++n) {
        const double x = rng.range(-5000.0, 5000.0);
        const double z = rng.range(-5000.0, 5000.0);
        const double reach = rng.range(0.0, 10.0);
        same = same && a.riverBandTopNear(x, z, reach) == terrain::kDryLevel &&
               b.riverBandTopNear(x, z, reach) == terrain::kDryLevel &&
               a.highestLevelNear(x, z, reach) == b.highestLevelNear(x, z, reach);
    }
    TEST_CHECK(same);
}

// The columns of a cold climate are cold, wet or dry: their rivers freeze, as their lakes; the
// columns of a humid climate are not.
void testCold() {
    const CaveField field(kSeed, galleries_test::uniformParams(CaveDistrictType::Rivers));
    const terrain::CaveGround flat = caves_test::flatGround(kFlatHeight);
    const terrain::CaveGround cold =
        caves_test::withEnvironment(flat, caves_test::coldEnvironment());
    const terrain::CaveGround humid =
        caves_test::withEnvironment(flat, caves_test::humidEnvironment());
    AquiferCache coldCells(field, cold);
    AquiferCache humidCells(field, humid);
    Rng rng(29);
    int coldColumns = 0;
    int humidColumns = 0;
    int rivers = 0;
    for (int n = 0; n < 400; ++n) {
        const double x = rng.range(-3000.0, 3000.0);
        const double z = rng.range(-3000.0, 3000.0);
        const CaveColumn a = field.column(x, z, kFlatHeight, cold, {}, &coldCells);
        const CaveColumn b = field.column(x, z, kFlatHeight, humid, {}, &humidCells);
        coldColumns += a.cold ? 1 : 0;
        humidColumns += b.cold ? 1 : 0;
        rivers += a.riverLevel > terrain::kDryLevel ? 1 : 0;
    }
    TEST_CHECK(coldColumns == 400 && humidColumns == 0 && rivers == 400);
}

} // namespace

int main() {
    testBandIsolation();
    testSill();
    testEntrancesClear();
    testInactive();
    testCold();
    return test::result();
}
